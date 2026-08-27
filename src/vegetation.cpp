#include "helpers.hpp"
#include "vegetation.hpp"

// Terrain foliage parameters
float TREE_DENSITY_MULTIPLIER = 1.0f;   // Global tree density multiplier

/* * Place trees in the specified chunk.
 * Amount of trees placed in the chunk is determined by the biome's tree density and a global multiplier.
 * * @param chunkX Chunk X coordinate
 * @param chunkY Chunk Y coordinate
 * @param chunkZ Chunk Z coordinate
 * @param chunkSize Size of the chunk (same as constant CHUNK_SIZE)
 * @param seed Random seed for tree placement
 * @param blocks 3D array of blocks in the chunk
 * @param centerColData Shared pointer to the column data for the chunk
 */
void Vegetation::placeTreesInChunk(int chunkX, int chunkY, int chunkZ, int chunkSize, int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE], const std::shared_ptr<HeightField::ColumnData>& centerColData)
{
    // 1. Hoist invariant math outside the chunk loops
    const int chunkBaseY = chunkY * chunkSize;
    const int canopyRadius = 2;
    const int minReach = chunkSize - canopyRadius;

    // Iterate 3x3 neighborhood (including center)
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {

            int nx = chunkX + dx;
            int nz = chunkZ + dz;

            int centerWorldX = nx * chunkSize + chunkSize / 2;
            int centerWorldZ = nz * chunkSize + chunkSize / 2;

            Biome::BiomeParams biome = Voronoi::sampleBlendedBiomeParams(centerWorldX, centerWorldZ, seed); // 36 - 44 microseconds

            // Early exit before hitting height noise
            if (biome.treeDensity <= 0.0f) continue;

            float cluster = Noise::heightNoise2D(centerWorldX * 0.01f, centerWorldZ * 0.01f, seed); // 20 - 25 microseconds
            cluster = (cluster + 1.0f) * 0.6f;

            // Re-generate deterministic base seed for this chunk
            uint32_t chunkBaseRNG = Noise::hash(nx, nz, seed);
            uint32_t countRNG = chunkBaseRNG;
            int treeCount = int(biome.treeDensity * cluster + Noise::rand01(countRNG) * TREE_DENSITY_MULTIPLIER);

            if (treeCount <= 0) continue;

            // 2. LAZY LOADING: We don't fetch neighbor columns yet!
            std::shared_ptr<HeightField::ColumnData> col = nullptr;
            if (dx == 0 && dz == 0) {
                col = centerColData; // We know we have the center chunk already
            }

            // Hoist local coordinate offsets
            const int offsetX = dx * chunkSize;
            const int offsetZ = dz * chunkSize;

            uint32_t treeRNG = Noise::hash(nx, nz, seed);

            for (int i = 0; i < treeCount; ++i) // 35 - 50 microseconds
            {
                uint32_t state = treeRNG;
                // use state for this tree

                // advance to next deterministic state uniformly for the NEXT tree
                treeRNG ^= treeRNG << 13;
                treeRNG ^= treeRNG >> 17;
                treeRNG ^= treeRNG << 5;

                // Local coordinate IN THE NEIGHBOR CHUNK
                int lx = int(Noise::rand01(state) * chunkSize);
                int lz = int(Noise::rand01(state) * chunkSize);

                // 3. FAST SPATIAL CULLING: Do this BEFORE touching heightmaps
                if (dx == -1 && lx < minReach) continue;
                if (dx == 1 && lx >= canopyRadius) continue;
                if (dz == -1 && lz < minReach) continue;
                if (dz == 1 && lz >= canopyRadius) continue;

                // 4. DEFERRED CACHE HIT: Only get the neighbor column data if a tree actually reaches us
                if (!col) {
                    col = ColumnCache::getOrGenerateColumn(nx, nz, seed);
                }

                int groundY = col->heightMap[lx][lz];

                // Height limit check
                if (groundY > biome.treeLine) { continue; }

                // Simple slope check 
                int hX = (lx + 1 < chunkSize) ? col->heightMap[lx + 1][lz] : groundY;
                int hZ = (lz + 1 < chunkSize) ? col->heightMap[lx][lz + 1] : groundY;

                if (std::abs(hX - groundY) > 2 || std::abs(hZ - groundY) > 2) { continue; }

                // Calculate RELATIVE coordinates in OUR chunk
                int relativeX = lx + offsetX;
                int relativeZ = lz + offsetZ;
                int relativeY = (groundY + 1) - chunkBaseY;


                placeTreeAt(relativeX, relativeY, relativeZ, state, blocks); // 34 - 50 microseconds
            }
        }
    }
}

/* * Place a single tree at the specified local coordinates.
 * If the trunk's position is outside the chunk, we won't return early, as it's leaves may still be in the chunk
 * * TODO: Add biome-specific tree types and variations (e.g., pine, oak, birch) based on the biome parameter.
 * * @param x Local X coordinate in the chunk (can be outside 0-15 for overhangs)
 * @param y Local Y coordinate in the chunk
 * @param z Local Z coordinate in the chunk
 * @param rng Random number generator seed
 * @param blocks internal 3D array of blocks for the chunk
 */
void Vegetation::placeTreeAt(int x, int y, int z, uint32_t& rng, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE])
{
    int trunkHeight = 2 + int(Noise::rand01(rng) * 3); // 2-4 blocks tall
    int top = y + trunkHeight;

    // 1. CONSOLIDATED TRUNK LOGIC
    // The original code placed the bottom of the trunk in one loop, and the top of the 
    // trunk inside the canopy loop. We can do it all at once to keep the canopy loop pure.
    if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE)
    {
        // Clamp vertical placement safely to chunk boundaries
        int startY = std::max(0, y - 1);
        int endY = std::min(CHUNK_SIZE - 1, top); // 'top' is inclusive based on original logic

        for (int ty = startY; ty <= endY; ++ty) {
            blocks[x][ty][z].setValues(BlockType::WOOD, 0, 0);
        }
    }

    // 2. LOOP CLAMPING (Intersection of canopy radius and chunk bounds)
    // This entirely eliminates the need for if (cx < 0 || cx >= CHUNK_SIZE ...) inside the loop
    const int radius = 2;
    int minCx = std::max(0, x - radius);
    int maxCx = std::min(CHUNK_SIZE - 1, x + radius);

    int minCz = std::max(0, z - radius);
    int maxCz = std::min(CHUNK_SIZE - 1, z + radius);

    // Leaves must be strictly above y + 1 (so they start at y + 2).
    int minCy = std::max(0, std::max(y + 2, top - radius));
    int maxCy = std::min(CHUNK_SIZE - 1, top + radius);

    // 3. OPTIMIZED CANOPY LOOP
    for (int cx = minCx; cx <= maxCx; ++cx) {
        // Hoist the X distance calculation
        int dxDist = std::abs(cx - x);

        for (int cy = minCy; cy <= maxCy; ++cy) {
            // Hoist the Y distance calculation
            int dyDist = std::abs(cy - top);

            for (int cz = minCz; cz <= maxCz; ++cz) {
                // Manhattan distance check
                int dist = dxDist + dyDist + std::abs(cz - z);
                if (dist > radius + 1) continue;

                // Cache the block reference to avoid recalculating the 3D array offset three times
                auto& block = blocks[cx][cy][cz];

                // Because we placed the full trunk first, it's no longer AIR, 
                // so we don't have to worry about leaves overwriting the trunk here.
                if (block.getType() == BlockType::AIR) {
                    block.setValues(BlockType::LEAVES, 0, 0);
                }
            }
        }
    }
}
