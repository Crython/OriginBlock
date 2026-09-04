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

	// Find the column in the cache first
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = columnCache.find(key);
        if (it != columnCache.end()) {
            return it->second;
        }
    }

    // Generate if not found - allocate on heap immediately
    auto data = std::make_shared<HeightField::ColumnData>();

    // Step 1: Initial height generation for the entire column
    HeightField::ColumnData cd = HeightField::generateHeightColumn(chunkX, chunkZ, seed);

    // Copy the values over 
    *data = cd;
    /*
    // Step 2: Apply 4-way slope-aware attenuation
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int worldX = chunkX * CHUNK_SIZE + x;
            int worldZ = chunkZ * CHUNK_SIZE + z;
            float h = data->heightMap[x][z];

            float hx, he, hz, hs;

            // West neighbor (x-1)
            if (x > 0) hx = data->heightMap[x - 1][z];
            else if (westCol) hx = westCol->heightMap[CHUNK_SIZE - 1][z];
            else hx = hf.generateHeightColumn(worldX - 1, worldZ, seed);

            // East neighbor (x+1)
            if (x < CHUNK_SIZE - 1) he = data->heightMap[x + 1][z];
            else if (eastCol) he = eastCol->heightMap[0][z];
            else he = HeightField::generateHeight(worldX + 1, worldZ, biomeParams, seed);

            // North neighbor (z-1)
            if (z > 0) hz = data->heightMap[x][z - 1];
            else if (northCol) hz = northCol->heightMap[x][CHUNK_SIZE - 1];
            else hz = HeightField::generateHeight(worldX, worldZ - 1, biomeParams, seed);

            // South neighbor (z+1)
            if (z < CHUNK_SIZE - 1) hs = data->heightMap[x][z + 1];
            else if (southCol) hs = southCol->heightMap[x][0];
            else hs = HeightField::generateHeight(worldX, worldZ + 1, biomeParams, seed);

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
    */

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
