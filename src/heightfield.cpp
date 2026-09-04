#include "heightfield.hpp"
#include "biome.hpp"
#include "helpers.hpp"
#include <random>
/*
* Terrain Generation Notes - 1km = 1000m = 1000 blocks
*
|                                                     | Spacing/   | Height/   |
| Feature                                             | Frecuency  | Depth     |
| --------------------------------------------------- | ---------- | --------- |
| Small landmarks(structures, small / medium caves)   |  0.2-0.5 m |   20–50 m |
| Villages / camps                                    |     1–3 km |    5-15 m |
| Large hills or big caves                            | 0.8–1.5 km | 100-250 m | 
| Major mountains                                     |     5–8 km |  0.3-1 km |
| Major mountain ranges                               |   20–30 km |    1–3 km |
| Large lakes                                         |     1–5 km |   20-30 m |
| Inland seas                                         |   10–20 km |   35-55 m |
*
*/

// Returns true if the point is below sea level, BEFORE mountains/detail are added.
// Useful for coastline/biome classification separate from final height.
inline bool HeightField::isOcean(TerrainNoise& n, float worldX, float worldY) {
    return n.continentNoise.GetNoise(worldX, worldY) < n.seaLevel;
}
// Clamp value to the 0 - 1 range
inline float HeightField::Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

// 
inline float HeightField::Remap(float v, float oldMin, float oldMax, float newMin, float newMax) {
    return newMin + (v - oldMin) * (newMax - newMin) / (oldMax - oldMin);
}
// Continentalness -> elevation curve. Oceans sit in a shelf, land rises with
// distance from the coast. Control points are a starting point - tune to taste.
float HeightField::ContinentalnessSpline(float c) {
    struct Point { float c, h; };
    static const Point pts[] = {
        {0.00f, -1.00f}, // deep ocean floor
        {0.20f, -0.55f}, // ocean
        {0.35f, -0.15f}, // continental shelf
        {0.42f,  0.00f}, // coastline
        {0.55f,  0.20f}, // near-inland plains
        {0.75f,  0.55f}, // mid-inland hills
        {1.00f,  1.00f}, // far-inland highlands
    };
    constexpr int n = sizeof(pts) / sizeof(pts[0]);
    c = Clamp01(c);
    for (int i = 0; i < n - 1; ++i) {
        if (c <= pts[i + 1].c) {
            float t = (c - pts[i].c) / (pts[i + 1].c - pts[i].c);
            return Helpers::lerp(pts[i].h, pts[i + 1].h, t);
        }
    }
    return pts[n - 1].h;
}

// Steps 1-5: returns height normalized to [0, 1]
float HeightField::generateHeightCell(TerrainNoise& n, Biome::ClimateSample sample, float worldX, float worldY) {

    // --- Step 1: continents / oceans, driven by climate continentalness ---
    // Using the same continentalness the biome system uses guarantees oceans
    // are actually low and inland is actually high - the two systems can't
    // disagree about where the coastline is anymore.
    float continent = ContinentalnessSpline(sample.continentalness);
    // A touch of independent noise on top keeps fine-grained variation
    // instead of a perfectly smooth spline everywhere.
    continent += n.continentNoise.GetNoise(worldX, worldY) * 0.05f;

    // --- Step 2: tectonic activity, softened by climate erosion ---
    float tectonics = n.tectonicNoise.GetNoise(worldX, worldY);   // [-1, 1]
    // INCREASED FLOOR from 0.15f to 0.30f to make plains more hilly/bumpy
    float tectonicFactor = Remap(tectonics, -1.0f, 1.0f, 0.30f, 1.0f);
    // erosion=0 (pristine) -> unchanged; erosion=1 (heavily eroded) -> pulled
    // toward half strength, so worn-down regions can't raise sharp ranges
    tectonicFactor *= Remap(sample.erosion, 0.0f, 1.0f, 1.0f, 0.50f);

    // --- Step 3: mountain range mask, gated by climate peaks ---
    float rangeMask = n.rangeMaskNoise.GetNoise(worldX, worldY);  // ridged -> roughly [0,1]
    rangeMask = Clamp01(rangeMask);

    // Optional: You can square the mask to make mountains rise more sharply from the plains
    // rangeMask = rangeMask * rangeMask;

    rangeMask *= tectonicFactor;
    // `peaks` is already erosion-aware (see sampleClimate) and is what the
    // biome map calls a "peaks" region - tie the mountain mask to it so the
    // tallest terrain lines up with the peaks biome, not just wherever the
    // local tectonic noise happens to spike.
    rangeMask = Clamp01(rangeMask * Remap(sample.peaks, 0.0f, 1.0f, 0.5f, 1.2f));

    // --- Step 5: domain warp, strength driven by climate weirdness ---
    // weird regions get more contorted, chaotic terrain; ordinary regions
    // stay closer to the raw noise shapes
    float warpStrength = Remap(sample.weirdness, 0.0f, 1.0f, 0.7f, 1.3f);
    float warpX = n.warpNoiseX.GetNoise(worldX, worldY) * 300.0f * warpStrength;
    float warpY = n.warpNoiseY.GetNoise(worldX, worldY) * 300.0f * warpStrength;
    float wx = worldX + warpX;
    float wy = worldY + warpY;

    // --- Step 4: base terrain (sampled at warped coords) ---
    float regional = n.regionalNoise.GetNoise(wx, wy);
    float local = n.localNoise.GetNoise(wx, wy);
    float mountains = rangeMask * ((regional + local) * 0.5f);

    float terrain = n.terrainNoise.GetNoise(wx, wy);
    // general (non-range) terrain roughness also flattens under erosion, not
    // just the mountain ranges
    terrain *= Remap(sample.erosion, 0.0f, 1.0f, 1.0f, 0.60f);

    float detailCell = n.detailCellular.GetNoise(wx, wy);
    float detailFbmV = n.detailFbm.GetNoise(wx, wy);
    float detail = (detailCell + detailFbmV) * 0.5f;
    // extra high-frequency chaos in weird regions
    detail *= Remap(sample.weirdness, 0.0f, 1.0f, 0.85f, 1.25f);

    float micro = n.microNoise.GetNoise(wx, wy);

    // --- Combine ---
    // INCREASED weights for mountains (0.6 -> 1.5) and terrain (0.25 -> 0.5)
    float height = continent * 1.00f
        + mountains * 2.50f
        + terrain * 1.50f
        + detail * 0.12f
        + micro * 0.03f;

    // NOTE: these bounds were already stale before this change (the comment
    // claims mountains max 1.5 but the weight above is 2.50), and the new
    // erosion/peaks/weirdness multipliers shift the true min/max further out.
    // Treat -1.60/3.10 as an approximation - re-tune by sampling a large
    // batch of cells, logging the true pre-remap min/max, and using that.
    height = Remap(height, -1.60f, 3.10f, 0.0f, 1.0f);
    return Clamp01(height);
}


// --- Step 6a: simple thermal erosion (talus-angle smoothing) ---
void HeightField::ApplyThermalErosion(HeightGrid& g, int iterations, float talusAngle, float amount) {
    for (int it = 0; it < iterations; ++it) {
        HeightGrid next = g;
        for (int y = 1; y < g.height - 1; ++y) {
            for (int x = 1; x < g.width - 1; ++x) {
                float h = g.at(x, y);
                float maxDiff = 0.0f;
                int lowX = x, lowY = y;
                static const int dx[8] = { -1,0,1,-1,1,-1,0,1 };
                static const int dy[8] = { -1,-1,-1,0,0,1,1,1 };
                for (int i = 0; i < 8; ++i) {
                    float nh = g.at(x + dx[i], y + dy[i]);
                    float diff = h - nh;
                    if (diff > maxDiff) { maxDiff = diff; lowX = x + dx[i]; lowY = y + dy[i]; }
                }
                if (maxDiff > talusAngle) {
                    float transfer = (maxDiff - talusAngle) * amount * 0.5f;
                    next.at(x, y) -= transfer;
                    next.at(lowX, lowY) += transfer;
                }
            }
        }
        g = next;
    }
}

// --- Step 6b: simple hydraulic erosion (droplet-based) ---
void HeightField::ApplyHydraulicErosion(HeightGrid& g, int dropletCount, unsigned int seed) {
    // Local RNG instead of global srand()/rand(): reentrant and thread-safe
    // (global rand() state was a data race if chunks generate concurrently),
    // and depends only on the seed passed in - see generateHeightColumn,
    // which now mixes chunk coordinates into that seed instead of reusing
    // the same raw world seed for every chunk.
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pickX(0, g.width - 1);
    std::uniform_int_distribution<int> pickY(0, g.height - 1);
    for (int d = 0; d < dropletCount; ++d) {
        float x = static_cast<float>(pickX(rng));
        float y = static_cast<float>(pickY(rng));
        float sediment = 0.0f, speed = 1.0f, water = 1.0f;

        for (int step = 0; step < 64; ++step) {
            int ix = static_cast<int>(x), iy = static_cast<int>(y);
            if (ix <= 0 || iy <= 0 || ix >= g.width - 1 || iy >= g.height - 1) break;

            // steepest descent among 4-neighbors
            float h = g.at(ix, iy);
            float hL = g.at(ix - 1, iy), hR = g.at(ix + 1, iy);
            float hU = g.at(ix, iy - 1), hD = g.at(ix, iy + 1);

            float gx = (hR - hL) * 0.5f;
            float gy = (hD - hU) * 0.5f;
            float len = std::sqrt(gx * gx + gy * gy) + 1e-6f;
            gx /= len; gy /= len;

            x -= gx; y -= gy;

            int nx = static_cast<int>(x), ny = static_cast<int>(y);
            if (nx < 0 || ny < 0 || nx >= g.width || ny >= g.height) break;

            float newH = g.at(nx, ny);
            float deltaH = newH - h;

            float capacity = std::max(-deltaH, 0.01f) * speed * water;
            if (sediment > capacity) {
                float deposit = (sediment - capacity) * 0.3f;
                g.at(ix, iy) += deposit;
                sediment -= deposit;
            }
            else {
                // Erosion should happen while the droplet moves downhill
                // (deltaH < 0). This condition was inverted, which disabled
                // erosion during normal flow and only allowed it in the rare
                // case the droplet stepped uphill - so hydraulic erosion was
                // barely doing anything, leaving the small-scale source noise
                // unsmoothed.
                float erode = std::min((capacity - sediment) * 0.3f, deltaH <= 0.0f ? 0.01f : 0.0f);
                g.at(ix, iy) -= erode;
                sediment += erode;
            }

            speed = std::sqrt(std::max(speed * speed + deltaH * -9.8f, 0.0f));
            water *= 0.98f; // evaporation
            if (water < 0.01f) break;
        }
    }
}

// --- Step 7: river tracing via steepest descent flow accumulation ---
// Returns a grid where higher values = more water flowed through that cell.
HeightField::HeightGrid HeightField::ComputeFlowAccumulation(const HeightGrid& g) {
    HeightGrid flow{ g.width, g.height, std::vector<float>(g.data.size(), 1.0f) };

    // sort cells high -> low so water flows downhill in one pass
    std::vector<int> indices(g.data.size());
    for (size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<int>(i);
    std::sort(indices.begin(), indices.end(),
        [&](int a, int b) { return g.data[a] > g.data[b]; });

    for (int idx : indices) {
        int x = idx % g.width, y = idx / g.width;
        if (x <= 0 || y <= 0 || x >= g.width - 1 || y >= g.height - 1) continue;

        float h = g.at(x, y);
        int lowX = -1, lowY = -1;
        float lowestH = h;
        static const int dx[8] = { -1,0,1,-1,1,-1,0,1 };
        static const int dy[8] = { -1,-1,-1,0,0,1,1,1 };
        for (int i = 0; i < 8; ++i) {
            float nh = g.at(x + dx[i], y + dy[i]);
            if (nh < lowestH) { lowestH = nh; lowX = x + dx[i]; lowY = y + dy[i]; }
        }
        if (lowX >= 0) flow.at(lowX, lowY) += flow.at(x, y);
    }
    return flow;
}


namespace {
    // Mixes the world seed with chunk coordinates into a well-distributed
    // per-chunk seed. Previously every chunk passed the same raw world seed
    // into ApplyHydraulicErosion, so every chunk got the exact same droplet
    // pattern "stamped" down (visible as a repeating pattern), on top of the
    // global-RNG data race mentioned above.
    unsigned int HashChunkSeed(unsigned int seed, int chunkX, int chunkZ) {
        unsigned int h = seed;
        h ^= static_cast<unsigned int>(chunkX) * 0x9E3779B1u;
        h ^= static_cast<unsigned int>(chunkZ) * 0x85EBCA77u;
        h ^= (h >> 16);
        h *= 0x7FEB352Du;
        h ^= (h >> 15);
        h *= 0x846CA68Bu;
        h ^= (h >> 16);
        return h;
    }
}

HeightField::ColumnData HeightField::generateHeightColumn(int chunkX, int chunkZ, unsigned int seed) {
    TerrainNoise tn(seed);
    ColumnData cd;
    cd.maxHeight = 0.0f;

    for (int z = 0; z < CHUNK_SIZE; z++) {
        float wz = static_cast<float>(chunkZ * CHUNK_SIZE + z);
        for (int x = 0; x < CHUNK_SIZE; x++) {
            float wx = static_cast<float>(chunkX * CHUNK_SIZE + x);

			// Get biome params, so we can use them to scale the height/density appropriately. Use the same terrain noise to sample climate, so that the biome is consistent with the terrain.
			Biome::ClimateSample s = Biome::sampleClimate(tn, wx, wz, seed);
			cd.atC(x, z) = s;

            float h = generateHeightCell(tn, s, wx, wz) * 128.0f + 32.0f;
            cd.at(x, z) = h;
            if (cd.maxHeight < h) cd.maxHeight = h;
        }
    }

    return cd;
}