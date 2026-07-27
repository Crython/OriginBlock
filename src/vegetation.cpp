#include "vegetation.hpp"

// Terrain foliage parameters
float TREE_DENSITY_MULTIPLIER = 1.0f;   // Global tree density multiplier

/*
 * Place trees in the specified chunk.
 * Amount of trees placed in the chunk is determined by the biome's tree density and a global multiplier.
 *
 * @param chunkX Chunk X coordinate
 * @param chunkY Chunk Y coordinate
 * @param chunkZ Chunk Z coordinate
 * @param chunkSize Size of the chunk (same as constant CHUNK_SIZE)
 * @param seed Random seed for tree placement
 * @param blocks 3D array of blocks in the chunk
 * @param centerColData Shared pointer to the column data for the chunk
 */
void Vegetation::placeTreesInChunk(int chunkX, int chunkY, int chunkZ, int chunkSize, int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE], const std::shared_ptr<HeightField::ColumnData>& centerColData)
{
    // Iterate 3x3 neighborhood (including center)
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {

            int nx = chunkX + dx;
            int nz = chunkZ + dz;

            // Use passed pointer for center, fetch for neighbors (efficient cache)
            auto col = (dx == 0 && dz == 0) ? centerColData : ColumnCache::getOrGenerateColumn(nx, nz, seed);

            // Re-generate deterministic base seed for this chunk
            uint32_t chunkBaseRNG = Noise::hash(nx, nz, seed);

            // Calculate tree count for this neighbor chunk
            int centerWorldX = nx * chunkSize + chunkSize / 2;
            int centerWorldZ = nz * chunkSize + chunkSize / 2;
            Biome::BiomeParams biome = Voronoi::sampleBlendedBiomeParams(centerWorldX, centerWorldZ, seed);

            if (biome.treeDensity <= 0.0f) continue;

            float cluster = Noise::heightNoise2D(centerWorldX * 0.01f, centerWorldZ * 0.01f, seed);
            cluster = (cluster + 1.0f) * 0.6f;

            // Use chunkBaseRNG consistently for treeCount
            uint32_t countRNG = chunkBaseRNG;
            int treeCount = int(biome.treeDensity * cluster + Noise::rand01(countRNG) * TREE_DENSITY_MULTIPLIER);

            for (int i = 0; i < treeCount; ++i)
            {
                // CRITICAL: Each tree slot MUST have its own deterministic seed
                // This ensures coordinate picking at i=1 doesn't depend on whether i=0 was drawn.
                uint32_t treeRNG = Noise::hash(nx, nz, seed + i + 1);

                // Local coordinate IN THE NEIGHBOR CHUNK
                int lx = int(Noise::rand01(treeRNG) * chunkSize);
                int lz = int(Noise::rand01(treeRNG) * chunkSize);

                // Optimization: Skip trees too far to reach us
                // Tree canopy radius is 2. 
                // lx=14 in neighbor reaches 14-16+2 = 0.
                // lx=13 in neighbor reaches 13-16+2 = -1 (misses).
                // So for dx=-1, we need lx >= 14.
                if (dx == -1 && lx < chunkSize - 2) continue;
                if (dx == 1 && lx >= 2) continue;
                if (dz == -1 && lz < chunkSize - 2) continue;
                if (dz == 1 && lz >= 2) continue;

                int groundY = col->heightMap[lx][lz];

                // Height limit check with tiny random variation
                float treeLine = biome.treeLine;
                if (groundY > treeLine) continue;

                // Simple slope check (approximate for neighbors)
                int hX = (lx + 1 < chunkSize) ? col->heightMap[lx + 1][lz] : groundY;
                int hZ = (lz + 1 < chunkSize) ? col->heightMap[lx][lz + 1] : groundY;
                if (abs(hX - groundY) > 2 || abs(hZ - groundY) > 2) continue;

                // Calculate RELATIVE coordinates in OUR chunk
                int relativeX = lx + (dx * chunkSize);
                int relativeZ = lz + (dz * chunkSize);
                int relativeY = (groundY + 1) - (chunkY * chunkSize);

                // treeRNG is passed by value/reference and used for tree properties (height, etc)
                // Since it's unique to this tree index, it's deterministic.
                placeTreeAt(relativeX, relativeY, relativeZ, treeRNG, blocks);
            }
        }
    }
}

/*
 * Place a single tree at the specified local coordinates.
 * If the trunk's position is outside the chunk, we won't return early, as it's leaves may still be in the chunk
 *
 * TODO: Add biome-specific tree types and variations (e.g., pine, oak, birch) based on the biome parameter.
 *
 * @param x Local X coordinate in the chunk (can be outside 0-15 for overhangs)
 * @param y Local Y coordinate in the chunk
 * @param z Local Z coordinate in the chunk
 * @param rng Random number generator seed
 * @param blocks internal 3D array of blocks for the chunk
 */
void Vegetation::placeTreeAt(int x, int y, int z, uint32_t& rng, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE])
{
    // No early return for X/Z
    // We might need to draw leaves even if trunk is outside this chunk.

    int trunkHeight = 2 + int(Noise::rand01(rng) * 3); // 2-4 blocks tall

    // Trunk - Only draw if column is valid in this chunk
    if (x >= 0 && x < CHUNK_SIZE && z >= 0 && z < CHUNK_SIZE)
    {
        for (int i = 0; i < trunkHeight; ++i) {
            int ty = y + i - 1;
            if (ty >= 0 && ty < CHUNK_SIZE) {
                blocks[x][ty][z].setValues(BlockType::WOOD, 0, 0);
            }
        }
    }

    // Canopy
    int radius = 2;
    int top = y + trunkHeight;
    for (int dx = -radius; dx <= radius; ++dx)
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dz = -radius; dz <= radius; ++dz)
            {
                int dist = abs(dx) + abs(dy) + abs(dz);
                if (dist > radius + 1) continue;

                // Calculate chunk-local coordinates
                int cx = x + dx;
                int cy = top + dy;
                int cz = z + dz;

                // STRICT BOUNDS CHECK - Do NOT wrap around
                if (cx < 0 || cx >= CHUNK_SIZE || cy < 0 || cy >= CHUNK_SIZE || cz < 0 || cz >= CHUNK_SIZE) continue;

                // Enforce that leaves are 2+ blocks above ground and that they don't overwrite the trunk
                if (dx == 0 && dz == 0 && dy <= 0) { blocks[cx][cy][cz].setValues(BlockType::WOOD, 0, 0); continue; }
                if (cy <= y + 1) continue;

                // Only place leaves if the block is currently air
                if (blocks[cx][cy][cz].getType() == BlockType::AIR) blocks[cx][cy][cz].setValues(BlockType::LEAVES, 0, 0);
            }
}
