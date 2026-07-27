#include "pch.h"
#include "voronoi.hpp"

// Voronoi-local constants (climate frequencies now live in biome.cpp via Biome::sampleClimate)
constexpr float VORONOI_JITTER = 0.7f;           // Site position randomization (0-1)
constexpr float VORONOI_JITTER_V  = 0.7f;
constexpr float OCEAN_THRESHOLD_V = 0.27f;
constexpr float BIOME_CELL_SIZE_V = 64.0f;

// Static member definitions
std::vector<VoronoiSite> Voronoi::voronoiSites;
VoronoiSpatialGrid Voronoi::voronoiGrid;


/**
 * Initialize the Voronoi cell system for biome distribution.
 * Creates a spatial grid of biome sites, each with climate-based characteristics.
 *
 * @param seed World generation seed
 * @param numSites Number of Voronoi sites to create
 * @param mapSize Total size of the area to cover
 * @param startX, startZ World coordinates of the area's origin
 */
void Voronoi::initVoronoi(int seed, int numSites, float mapSize, float startX, float startZ) {
    voronoiSites.clear();
    voronoiGrid.clear();

    voronoiGrid.startX = startX;
    voronoiGrid.startZ = startZ;
    voronoiGrid.mapSize = mapSize;

    int gridSize = static_cast<int>(std::sqrt(static_cast<float>(numSites)));
    if (gridSize < 1) gridSize = 1;
    float spacing = mapSize / static_cast<float>(gridSize);

    // Grid settings
    voronoiGrid.cellSize = spacing * 1.5f; // Slightly larger than spacing for overlap
    voronoiGrid.cols = static_cast<int>(std::ceil(mapSize / voronoiGrid.cellSize)) + 1;
    voronoiGrid.rows = static_cast<int>(std::ceil(mapSize / voronoiGrid.cellSize)) + 1;
    voronoiGrid.grid.resize(static_cast<size_t>(voronoiGrid.cols) * voronoiGrid.rows);

    for (int i = 0; i < numSites; ++i) {
        int row = i / gridSize;
        int col = i % gridSize;
        float baseSx = startX + col * spacing;
        float baseSz = startZ + row * spacing;

        // Add jitter to avoid perfectly regular grid
        float jitterAmp = spacing * VORONOI_JITTER_V;
        float sx = baseSx + Noise::heightNoise2D(baseSx, baseSz, seed + 1000) * jitterAmp - jitterAmp * 0.5f;
        float sz = baseSz + Noise::heightNoise2D(baseSx * 1.1f, baseSz * 0.9f, seed + 1001) * jitterAmp - jitterAmp * 0.5f;

        // Warp sites slightly for less robotic feel
        float wsx = sx, wsz = sz;
        Noise::domainWarp(wsx, wsz, seed + 555);

        // Assign climate parameters via the shared helper
        Biome::ClimateSample climate = Biome::sampleClimate(wsx, wsz, seed);

        Biome::BiomeType siteBiome = Biome::computeBiomeFromClimate(
            climate.temp, climate.moisture, climate.weird, climate.continent);
        size_t siteIdx = voronoiSites.size();
        voronoiSites.push_back({ sx, sz, siteBiome });

        // Add to grid
        int gx = static_cast<int>((sx - startX) / voronoiGrid.cellSize);
        int gz = static_cast<int>((sz - startZ) / voronoiGrid.cellSize);
        if (gx >= 0 && gx < voronoiGrid.cols && gz >= 0 && gz < voronoiGrid.rows) {
            voronoiGrid.grid[gz * voronoiGrid.cols + gx].push_back(siteIdx);
        }
    }
    std::cout << "Initialized " << voronoiSites.size() << " Voronoi biome sites." << std::endl;
}

/**
 * Sample the biome at a specific cell position.
 * Uses domain-warped Voronoi nearest-neighbor with jittered boundaries.
 * Ocean biomes override Voronoi cells below the continent threshold.
 */
Biome::BiomeType Voronoi::sampleBiomeCell(int bx, int bz, int seed) {
    float x = static_cast<float>(bx);
    float z = static_cast<float>(bz);

    // Apply domain warping to bridge Voronoi cell boundaries naturally
    float wx = x, wz = z;
    Noise::domainWarp(wx, wz, seed);

    // Find nearest Voronoi site using spatial grid
    float minDist = std::numeric_limits<float>::max();
    Biome::BiomeType nearestBiome = Biome::BiomeType::None;

    int gx = static_cast<int>((x - voronoiGrid.startX) / voronoiGrid.cellSize);
    int gz = static_cast<int>((z - voronoiGrid.startZ) / voronoiGrid.cellSize);

    // Add high-frequency jitter for "jagged" boundaries
    float jx = wx + Noise::openSimplex2(wx * 0.15f, wz * 0.15f, seed + 123) * 3.5f;
    float jz = wz + Noise::openSimplex2(wx * 0.15f + 7.7f, wz * 0.15f + 3.3f, seed + 456) * 3.5f;

    // Check current cell and neighbors
    for (int dz = -2; dz <= 2; ++dz) { // Increased search radius slightly for safety with jitter
        for (int dx = -2; dx <= 2; ++dx) {
            int ngx = gx + dx;
            int ngz = gz + dz;
            if (ngx >= 0 && ngx < voronoiGrid.cols && ngz >= 0 && ngz < voronoiGrid.rows) {
                const auto& cellSites = voronoiGrid.grid[ngz * voronoiGrid.cols + ngx];
                for (size_t siteIdx : cellSites) {
                    const auto& site = voronoiSites[siteIdx];

                    float ddx = jx - site.x;
                    float ddz = jz - site.z;
                    float dist = ddx * ddx + ddz * ddz;
                    if (dist < minDist) {
                        minDist = dist;
                        nearestBiome = site.biome;
                    }
                }
            }
        }
    }

    // Fallback if no sites found in proximity
    if (nearestBiome == Biome::BiomeType::None) {
        minDist = std::numeric_limits<float>::max();
        for (const auto& site : voronoiSites) {
            float ddx = wx - site.x;
            float ddz = wz - site.z;
            float dist = ddx * ddx + ddz * ddz;
            if (dist < minDist) {
                minDist = dist;
                nearestBiome = site.biome;
            }
        }
    }

    // Global climate parameters for ocean detection (shared formula via Biome::sampleClimate)
    Biome::ClimateSample climate = Biome::sampleClimate(wx, wz, seed);

    // Dynamic threshold for more natural, noisy coastlines
    float beachNoise = 0.5f; // openSimplex2(wx * 0.1f, wz * 0.1f, seed + 888) * BEACH_NOISE_SCALE;
    float dynamicThreshold = OCEAN_THRESHOLD_V + beachNoise;

    // Decision logic using overrides (e.g. Ocean)
    if (climate.continent < dynamicThreshold) {
        if (climate.temp > 0.7f) return Biome::BiomeType::WarmOcean;
        else if (climate.temp < 0.3f) return Biome::BiomeType::ArticOcean;
        else return Biome::BiomeType::Ocean;
    }

    // Otherwise, use the nearest Voronoi-assigned biome
    return nearestBiome;
}

/*
 * Sample blended biome parameters at a world position.
 * Performs smoothly-interpolated bilinear blending between 4 neighboring biome cells.
 *
 * Optimizations:
 *   1. Early-exit when all 4 corners share the same biome — avoids all blend work.
 *   2. Deduplication of getParams() for corners that share a biome type.
 *   3. Smoothstep on fx/fz to remove linear-ramp seam artifacts for free.
 */
Biome::BiomeParams Voronoi::sampleBlendedBiomeParams(int worldX, int worldZ, int seed)
{
    int bx = Noise::floorDiv(worldX, static_cast<int>(BIOME_CELL_SIZE_V));
    int bz = Noise::floorDiv(worldZ, static_cast<int>(BIOME_CELL_SIZE_V));

    Biome::BiomeType b00 = sampleBiomeCell(bx,     bz,     seed);
    Biome::BiomeType b10 = sampleBiomeCell(bx + 1, bz,     seed);
    Biome::BiomeType b01 = sampleBiomeCell(bx,     bz + 1, seed);
    Biome::BiomeType b11 = sampleBiomeCell(bx + 1, bz + 1, seed);

    // (1) Early-exit: if all four corners are the same biome, no blending needed.
    if (b00 == b10 && b00 == b01 && b00 == b11) {
        return Biome::getParams(b00);
    }

    // (2) Deduplicate getParams() — only call it once per unique biome type.
    Biome::BiomeParams p00 = Biome::getParams(b00);
    Biome::BiomeParams p10 = (b10 == b00) ? p00 : Biome::getParams(b10);
    Biome::BiomeParams p01 = (b01 == b00) ? p00 : (b01 == b10) ? p10 : Biome::getParams(b01);
    Biome::BiomeParams p11 = (b11 == b00) ? p00 : (b11 == b10) ? p10 : (b11 == b01) ? p01 : Biome::getParams(b11);

    // (3) Smoothstep the fractions to ease in/out at cell boundaries.
    float fx = float(worldX - bx * static_cast<int>(BIOME_CELL_SIZE_V)) / BIOME_CELL_SIZE_V;
    float fz = float(worldZ - bz * static_cast<int>(BIOME_CELL_SIZE_V)) / BIOME_CELL_SIZE_V;
    fx = fx * fx * (3.0f - 2.0f * fx);  // smoothstep(fx)
    fz = fz * fz * (3.0f - 2.0f * fz);  // smoothstep(fz)

    // Bilinear interpolation weights
    float w00 = (1.0f - fx) * (1.0f - fz);
    float w10 = fx          * (1.0f - fz);
    float w01 = (1.0f - fx) * fz;
    float w11 = fx          * fz;

    Biome::BiomeParams result;
    result.amplitude        = p00.amplitude        * w00 + p10.amplitude        * w10 + p01.amplitude        * w01 + p11.amplitude        * w11;
    result.baseHeight       = p00.baseHeight       * w00 + p10.baseHeight       * w10 + p01.baseHeight       * w01 + p11.baseHeight       * w11;
    result.mountainStrength = p00.mountainStrength * w00 + p10.mountainStrength * w10 + p01.mountainStrength * w01 + p11.mountainStrength * w11;
    result.treeDensity      = p00.treeDensity      * w00 + p10.treeDensity      * w10 + p01.treeDensity      * w01 + p11.treeDensity      * w11;
    result.treeLine         = p00.treeLine         * w00 + p10.treeLine         * w10 + p01.treeLine         * w01 + p11.treeLine         * w11;

    return result;
}
