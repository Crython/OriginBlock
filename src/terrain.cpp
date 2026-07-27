/*
 * TERRAIN.CPP
 * 
 * Procedural terrain generation system using multi-layered noise and Voronoi diagrams.
 * 
 * GENERATION APPROACH:
 * - Base heightmap: Multi-octave OpenSimplex2 noise with continent-scale features
 * - Biome system: Voronoi cells determine biome distribution based on climate parameters
 * - Climate layers: Temperature, moisture, weirdness (for mountains), and continent shaping
 * - Domain warping: Adds natural-looking distortion to reduce grid artifacts
 * 
 * NOISE LAYERS:
 * 1. Continental: Very low frequency (~0.0001) for landmass vs ocean
 * 2. Regional: Low frequency (~0.005) for hills and valleys  
 * 3. Local: Medium frequency (~0.01-0.1) for terrain detail
 * 4. Ridged: High frequency with ridge inversion for mountains
 */
#include "pch.h"
#include "terrain.hpp"
#include "helpers.hpp"

// Procedural generation of chunk blocks
void Terrain::generate( const ChunkCoord& chunkPos, const int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE])
{
	/* Time profiling of terrain generation:
    6% cache
    31% block filling
    63% tree placement
    */

    if (chunkPos.y < 0) return; // No chunks under negative chunkPos

    // Retrieve column data (cached shared_ptr)
	auto colData = ColumnCache::getOrGenerateColumn(chunkPos.x, chunkPos.z, seed); // 3.5 - 6 microseconds

    // The maximum height in this chunk column in chunk-space
    int maxBlockYInChunkPOS = Noise::floorDiv(colData->maxHeight, CHUNK_SIZE);

    // Padding to avoid generating chunks that are too far above the terrain
    // No tree or structure can be chunkExclusionPadding * ChunkSize(16) blocks tall, as the rest won't be generated
	constexpr int chunkExclusionPadding = 2;

    // The chunk is above the highest point in the column - chunk will be empty
    if (chunkPos.y > maxBlockYInChunkPOS + chunkExclusionPadding) return; // Slight padding because trees aren't included in the max height calculations
    
    int worldYHalf = chunkPos.y * CHUNK_SIZE;

	// Fill the chunk with blocks based on the heightmap and biome 
	for (int x = 0; x < CHUNK_SIZE; x++) { // 17 - 24 microseconds
        for (int z = 0; z < CHUNK_SIZE; z++) {

            int height = colData->heightMap[x][z];
            Biome::BiomeType biome = colData->biome;

            int worldX = chunkPos.x * CHUNK_SIZE + x;
            int worldZ = chunkPos.z * CHUNK_SIZE + z; // Still needed for hash calculation below

            for (int y = 0; y < CHUNK_SIZE; y++) {
                int worldY = worldYHalf + y;
                _Block& b = blocks[x][y][z];
                
                if (worldY < height - 2)
                    b.setValues(BlockType::STONE, 0, 0);      // stone
                else if (worldY < height - 1)
                    b.setValues(((int)biome <= 3 ? BlockType::SAND : (biome == Biome::BiomeType::Mountains ? BlockType::STONE : BlockType::DIRT)), 0, 0);      // dirt
                else if (worldY == height - 1)
                    b.setValues(((int)biome <= 3 ? BlockType::SAND : (biome == Biome::BiomeType::Mountains ? BlockType::STONE : BlockType::GRASS)), 0, 0);      // grass or sand
                else
                    b.setValues(BlockType::AIR, 0, 0);      // air
            }
			// Add bedrok layer seperately to avoid calling the hash function for every block in the chunk. Only call it for the bottom 3 layers.
            for (int y = 0; y < 3; y++) {
				int worldY = worldYHalf + y;
                if (worldY == 0 || (worldY == 1 && Noise::hash(worldX, worldZ, seed) % 3 == 0) || (worldY == 2 && Noise::hash(worldX, worldZ, seed) % 5 == 0))
                    blocks[x][y][z].setValues(BlockType::BEDROCK, 0, 0);      // bedrock
            }
        }
    }

	// Generate trees in the chunk
    Vegetation::placeTreesInChunk(chunkPos.x, chunkPos.y, chunkPos.z, CHUNK_SIZE, seed, blocks, colData); // 40 - 65 microseconds
}

uint32_t Terrain::setRandSeed(void* instancePtr) {
    int local_var;

	// Make sure that each call to setRandSeed returns a different value, even if called from the same instancePtr
    static int offset = 0;
	local_var = offset++;

    // Cast data pointer to uintptr_t
    uintptr_t p1 = reinterpret_cast<uintptr_t>(&local_var);

    // Cast pointer to uintptr_t directly
    uintptr_t p2 = reinterpret_cast<uintptr_t>(instancePtr);

    // Combine the two addresses in some way
    uint32_t combined = Noise::hash(p1, p2, Noise::mix(p1 + p2));

    return combined;
}
