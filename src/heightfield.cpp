#include "heightfield.hpp"
#include "helpers.hpp"
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

// Steps 1-5: returns height normalized to [0, 1]
float HeightField::generateHeightCell(TerrainNoise& n, float worldX, float worldY) {
    // --- Step 1: continents / oceans (never warped, sets the big picture) ---
    float continent = n.continentNoise.GetNoise(worldX, worldY); // [-1, 1]

    // --- Step 2: tectonic activity ---
    float tectonics = n.tectonicNoise.GetNoise(worldX, worldY);   // [-1, 1]
    // Keep a floor so flat plains still get some subtle variation
    float tectonicFactor = Remap(tectonics, -1.0f, 1.0f, 0.15f, 1.0f);

    // --- Step 3: mountain range mask ---
    float rangeMask = n.rangeMaskNoise.GetNoise(worldX, worldY);  // ridged -> roughly [0,1]
    rangeMask = Clamp01(rangeMask);
    rangeMask *= tectonicFactor;

    // --- Step 5: domain warp (applied to everything except continent) ---
    float warpX = n.warpNoiseX.GetNoise(worldX, worldY) * 300.0f;
    float warpY = n.warpNoiseY.GetNoise(worldX, worldY) * 300.0f;
    float wx = worldX + warpX;
    float wy = worldY + warpY;

    // --- Step 4: base terrain (sampled at warped coords) ---
    float regional = n.regionalNoise.GetNoise(wx, wy);
    float local = n.localNoise.GetNoise(wx, wy);
    float mountains = rangeMask * ((regional + local) * 0.5f);

    float terrain = n.terrainNoise.GetNoise(wx, wy);

    float detailCell = n.detailCellular.GetNoise(wx, wy);
    float detailFbmV = n.detailFbm.GetNoise(wx, wy);
    float detail = (detailCell + detailFbmV) * 0.5f;

    float micro = n.microNoise.GetNoise(wx, wy);

    // --- Combine (weights control how much each layer dominates the silhouette) ---
    float height = continent * 1.00f
        + mountains * 0.60f
        + terrain * 0.25f
        + detail * 0.08f
        + micro * 0.02f;

    // Normalize from the theoretical combined range to [0, 1]
    height = Remap(height, -1.95f, 1.95f, 0.0f, 1.0f);
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
    srand(seed);
    for (int d = 0; d < dropletCount; ++d) {
        float x = static_cast<float>(rand() % g.width);
        float y = static_cast<float>(rand() % g.height);
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
                float erode = std::min((capacity - sediment) * 0.3f, -deltaH >= 0 ? 0.0f : 0.01f);
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

HeightField::ColumnData HeightField::generateHeightColumn(int chunkX, int chunkZ, unsigned int seed) {

    // Initialize noise
    TerrainNoise tn(seed);

    // Initialize the HeightGrid
    HeightGrid hg;
    hg.init(CHUNK_SIZE, CHUNK_SIZE);

    // Generate the height of the column
    for (int z = 0; z < CHUNK_SIZE; z++) {
        float wz = chunkZ * CHUNK_SIZE + z;
        for (int x = 0; x < CHUNK_SIZE; x++) {
            float wx = chunkX * CHUNK_SIZE + x;

            // Generate normalized height [0, 1] and scale to block height
            hg.at(x, z) = generateHeightCell(tn, wx, wz) * 128.0f + 32.0f;
        }
    }

    ApplyThermalErosion(hg, 5, 0.02f, 0.5f);
    ApplyHydraulicErosion(hg, 512, seed);
    ComputeFlowAccumulation(hg);

    // Transfer the data to another struct
    ColumnData cd;
    cd.maxHeight = 0; // Initialize maxHeight
    for (int z = 0; z < CHUNK_SIZE; z++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            float h = hg.at(x, z);
            cd.at(x, z) = h;
            cd.maxHeight = (cd.maxHeight < h) ? h : cd.maxHeight;
        }
    }

    return cd;
}