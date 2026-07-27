#include "column_cache.hpp"

std::unordered_map<uint64_t, std::shared_ptr<HeightField::ColumnData>> ColumnCache::columnCache;
std::mutex ColumnCache::cacheMutex;

// Clears the entire column cache. This function is thread-safe and will lock the cache during the clearing operation to prevent data races
void ColumnCache::clearCache() {
	std::lock_guard<std::mutex> lock(cacheMutex);
	columnCache.clear();
}

// Return shared_ptr to avoid large copies and ensure pointer stability
std::shared_ptr<HeightField::ColumnData> ColumnCache::getOrGenerateColumn(int chunkX, int chunkZ, int seed) {
    uint64_t key = packCoords(chunkX, chunkZ);

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = columnCache.find(key);
        if (it != columnCache.end()) {
            return it->second;
        }
    }

    // Generate if not found - allocate on heap immediately
    auto data = std::make_shared<HeightField::ColumnData>();

    Biome::BiomeType chunkBiome = Voronoi::sampleBiomeCell(chunkX, chunkZ, seed);
    Biome::BiomeParams biomeParams = Voronoi::sampleBlendedBiomeParams(chunkX * CHUNK_SIZE + CHUNK_SIZE / 2, chunkZ * CHUNK_SIZE + CHUNK_SIZE / 2, seed);
    data->biome = chunkBiome;

    // Retrieve neighbors from cache to handle slope-aware attenuation across chunk boundaries
    std::shared_ptr<HeightField::ColumnData> westCol = nullptr, eastCol = nullptr, northCol = nullptr, southCol = nullptr;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto itW = columnCache.find(packCoords(chunkX - 1, chunkZ));
        if (itW != columnCache.end()) westCol = itW->second;
        auto itE = columnCache.find(packCoords(chunkX + 1, chunkZ));
        if (itE != columnCache.end()) eastCol = itE->second;
        auto itN = columnCache.find(packCoords(chunkX, chunkZ - 1));
        if (itN != columnCache.end()) northCol = itN->second;
        auto itS = columnCache.find(packCoords(chunkX, chunkZ + 1));
        if (itS != columnCache.end()) southCol = itS->second;
    }

    // Step 1: Initial height generation for the entire column
    int maxHeightFound = 0;
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            uint16_t h = (uint16_t)HeightField::generateHeight(chunkX * CHUNK_SIZE + x, chunkZ * CHUNK_SIZE + z, biomeParams, seed);
            data->heightMap[x][z] = h;

            maxHeightFound = (h > maxHeightFound) ? h : maxHeightFound; // get the highest pointin that XZ position
        }
    }

    data->maxHeight = (uint16_t)maxHeightFound;

    // Step 2: Apply 4-way slope-aware attenuation
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int worldX = chunkX * CHUNK_SIZE + x;
            int worldZ = chunkZ * CHUNK_SIZE + z;
            float h = (float)data->heightMap[x][z];

            float hx, he, hz, hs;

            // West neighbor (x-1)
            if (x > 0) hx = (float)data->heightMap[x - 1][z];
            else if (westCol) hx = (float)westCol->heightMap[CHUNK_SIZE - 1][z];
            else hx = (float)HeightField::generateHeight(worldX - 1, worldZ, biomeParams, seed);

            // East neighbor (x+1)
            if (x < CHUNK_SIZE - 1) he = (float)data->heightMap[x + 1][z];
            else if (eastCol) he = (float)eastCol->heightMap[0][z];
            else he = (float)HeightField::generateHeight(worldX + 1, worldZ, biomeParams, seed);

            // North neighbor (z-1)
            if (z > 0) hz = (float)data->heightMap[x][z - 1];
            else if (northCol) hz = (float)northCol->heightMap[x][CHUNK_SIZE - 1];
            else hz = (float)HeightField::generateHeight(worldX, worldZ - 1, biomeParams, seed);

            // South neighbor (z+1)
            if (z < CHUNK_SIZE - 1) hs = (float)data->heightMap[x][z + 1];
            else if (southCol) hs = (float)southCol->heightMap[x][0];
            else hs = (float)HeightField::generateHeight(worldX, worldZ + 1, biomeParams, seed);

            // Hard ridge cap
            float slope = Helpers::slopeMagnitude(h, hx, hz);
            const float RIDGE_MAX = 8.0f;
            if (slope > RIDGE_MAX) {
                float excess = slope - RIDGE_MAX;
                h -= excess * 0.75f;
            }

            // Sequential slope limiting
            h = hx + Helpers::slopeLimit(h - hx, 3.0f);
            h = he + Helpers::slopeLimit(h - he, 3.0f);
            h = hz + Helpers::slopeLimit(h - hz, 3.0f);
            h = hs + Helpers::slopeLimit(h - hs, 3.0f);

            data->heightMap[x][z] = (uint16_t)h;
        }
    }


    // Step 3: Hard ridge cap at 8 blocks
    HeightField::capRidges(*data, westCol.get(), eastCol.get(), northCol.get(), southCol.get());

    // Step 4: Apply thermal erosion with boundary awareness
    HeightField::thermalErosion(*data, westCol.get(), eastCol.get(), northCol.get(), southCol.get());

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        // Check again if inserted by another thread
        auto it = columnCache.find(key);
        if (it != columnCache.end()) {
            return it->second;
        }

        columnCache[key] = data;
        return data;
    }
}
