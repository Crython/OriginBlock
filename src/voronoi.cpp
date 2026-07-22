#include "pch.h"
#include "voronoi.hpp"

// Frequency scales (shared with terrain.cpp)
constexpr float CONTINENT_FREQUENCY_V = 0.0001f;
constexpr float CLIMATE_FREQUENCY_V   = 0.005f;
constexpr float WEIRD_FREQUENCY_V     = 0.003f;
constexpr float VORONOI_JITTER_V      = 0.7f;
constexpr float OCEAN_THRESHOLD_V     = 0.27f;
constexpr float CONTINENT_SCALE_V     = 1.17f;
constexpr float BIOME_CELL_SIZE_V     = 64.0f;

// Static member definitions
std::vector<VoronoiSite> Voronoi::voronoiSites;
VoronoiSpatialGrid Voronoi::voronoiGrid;

// Clamps a floating-point value to [0.0, 1.0].
static inline float vClamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

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

        // Assign climate parameters using multi-frequency noise
        const float MAIN_CLIMATE_SCALE = CLIMATE_FREQUENCY_V;
        const float WEIRD_SCALE        = WEIRD_FREQUENCY_V;

        // Temperature: Varies with latitude + noise
        float temp = 0.2f + 0.3f * std::sin(wsz * 0.0005f) + 0.58f * (Noise::openSimplex2(wsx * MAIN_CLIMATE_SCALE, wsz * MAIN_CLIMATE_SCALE, seed + 731) * 0.5f + 0.5f);
        temp = vClamp01(temp);

        // Moisture: Independent noise layer
        float moisture = Noise::openSimplex2(wsx * MAIN_CLIMATE_SCALE * 1.35f, wsz * MAIN_CLIMATE_SCALE * 1.35f, seed + 1249) * 0.5f + 0.5f;
        moisture = vClamp01(moisture);

        // Weirdness: Ridged noise for mountain/unusual terrain
        float weird_raw = Noise::openSimplex2(wsx * WEIRD_SCALE, wsz * WEIRD_SCALE, seed + 3791);
        float weird = Noise::ridge(weird_raw);
        weird = vClamp01(weird * 1.f);

        // Continental scale: Determines if area is land or ocean
        float continent = Noise::fbmContinent(wsx, wsz, seed + 5000, CONTINENT_FREQUENCY_V);
        continent = vClamp01(continent * CONTINENT_SCALE_V);

        Biome::BiomeType siteBiome = Biome::computeBiomeFromClimate(temp, moisture, weird, continent);
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

    // Global climate parameters for ocean detection
    float continent = Noise::fbmContinent(wx, wz, seed + 5000, CONTINENT_FREQUENCY_V);
    continent = vClamp01(continent * CONTINENT_SCALE_V);

    const float CLIMATE_SCALE = CLIMATE_FREQUENCY_V;
    float temp = 0.2f + 0.3f * std::sin(wz * 0.0005f) + 0.58f * (Noise::openSimplex2(wx * CLIMATE_SCALE, wz * CLIMATE_SCALE, seed + 731) * 0.5f + 0.5f);
    temp = vClamp01(temp);

    // Dynamic threshold for more natural, noisy coastlines
    float beachNoise = 0.5f; // openSimplex2(wx * 0.1f, wz * 0.1f, seed + 888) * BEACH_NOISE_SCALE;
    float dynamicThreshold = OCEAN_THRESHOLD_V + beachNoise;

    // Decision logic using overrides (e.g. Ocean)
    if (continent < dynamicThreshold) {
        if (temp > 0.7f) return Biome::BiomeType::WarmOcean;
        else if (temp < 0.3f) return Biome::BiomeType::ArticOcean;
        else return Biome::BiomeType::Ocean;
    }

    // Otherwise, use the nearest Voronoi-assigned biome
    return nearestBiome;
}

/*
 * Sample blended biome parameters at a world position.
 * Performs bilinear interpolation between 4 neighboring biome cells.
 */
Biome::BiomeParams Voronoi::sampleBlendedBiomeParams(int worldX, int worldZ, int seed)
{
    int bx = Noise::floorDiv(worldX, static_cast<int>(BIOME_CELL_SIZE_V));
    int bz = Noise::floorDiv(worldZ, static_cast<int>(BIOME_CELL_SIZE_V));

    float fx = float(worldX - bx * static_cast<int>(BIOME_CELL_SIZE_V)) / BIOME_CELL_SIZE_V;
    float fz = float(worldZ - bz * static_cast<int>(BIOME_CELL_SIZE_V)) / BIOME_CELL_SIZE_V;

    Biome::BiomeType b00 = sampleBiomeCell(bx,     bz,     seed);
    Biome::BiomeType b10 = sampleBiomeCell(bx + 1, bz,     seed);
    Biome::BiomeType b01 = sampleBiomeCell(bx,     bz + 1, seed);
    Biome::BiomeType b11 = sampleBiomeCell(bx + 1, bz + 1, seed);

    Biome::BiomeParams p00 = Biome::getParams(b00);
    Biome::BiomeParams p10 = Biome::getParams(b10);
    Biome::BiomeParams p01 = Biome::getParams(b01);
    Biome::BiomeParams p11 = Biome::getParams(b11);

    // Bilinear interpolation weights
    float w00 = (1 - fx) * (1 - fz);
    float w10 = fx       * (1 - fz);
    float w01 = (1 - fx) * fz;
    float w11 = fx       * fz;

    Biome::BiomeParams result;
    result.amplitude       = p00.amplitude       * w00 + p10.amplitude       * w10 + p01.amplitude       * w01 + p11.amplitude       * w11;
    result.baseHeight      = p00.baseHeight      * w00 + p10.baseHeight      * w10 + p01.baseHeight      * w01 + p11.baseHeight      * w11;
    result.mountainStrength= p00.mountainStrength* w00 + p10.mountainStrength* w10 + p01.mountainStrength* w01 + p11.mountainStrength* w11;
    result.treeDensity     = p00.treeDensity     * w00 + p10.treeDensity     * w10 + p01.treeDensity     * w01 + p11.treeDensity     * w11;
    result.treeLine        = p00.treeLine        * w00 + p10.treeLine        * w10 + p01.treeLine        * w01 + p11.treeLine        * w11;

    return result;
}
