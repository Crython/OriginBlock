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
#include "helpers.hpp"
#include "terrain.hpp"
#include "voronoi.hpp"

// ***************************
// NOISE GENERATION CONSTANTS
// ***************************

// Frequency scales for different noise layers
constexpr float CONTINENT_FREQUENCY = 0.0001f;  // Largest landmass features
constexpr float REGIONAL_FREQUENCY = 0.01f;      // Hills and broad valleys
constexpr float CLIMATE_FREQUENCY = 0.005f;      // Temperature and moisture variation
constexpr float WARP_FREQUENCY_LOW = 0.005f;     // Large-scale domain warping
constexpr float WARP_FREQUENCY_HIGH = 0.02f;     // Detail domain warping
constexpr float WEIRD_FREQUENCY = 0.003f;        // Mountain/weirdness indicator

// Amplitude/blend weights for noise combination
constexpr float CONTINENT_WEIGHT = 0.7f;         // Continental noise contribution
constexpr float REGIONAL_WEIGHT = 0.3f;          // Regional noise contribution
constexpr float CONTINENT_BOOST = 2.0f;          // Contrast boost exponent
constexpr float CONTINENT_SCALE = 1.17f;         // Final continent multiplier

// Domain warping parameters
constexpr float WARP_AMPLITUDE_LOW = 40.0f;      // Large displacement
constexpr float WARP_AMPLITUDE_HIGH = 8.0f;      // Fine displacement

// Biome system parameters
constexpr int BIOME_CELL_SIZE = 64;              // Base grid size for biomes (in blocks)
constexpr float OCEAN_THRESHOLD = 0.27f;         // Continent value below which is ocean
constexpr float BEACH_NOISE_SCALE = 0.06f;       // Coastline variation

// Voronoi jitter
constexpr float VORONOI_JITTER = 0.7f;           // Site position randomization (0-1)

// Procedural generation of chunk blocks
void Terrain::generate( const ChunkCoord& chunkPos, const int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE])
{
    if (chunkPos.y < 0) return; // No chunks under negative chunkPos

    // Retrieve column data (cached shared_ptr)
    auto colData = ColumnCache::getOrGenerateColumn(chunkPos.x, chunkPos.z, seed);
    
    // The maximum height in this chunk column in chunk-space
    int maxBlockYInChunkPOS = Noise::floorDiv(colData->maxHeight, CHUNK_SIZE);

    // The chunk is above the highest point in the column - chunk will be empty
    if (chunkPos.y > maxBlockYInChunkPOS + 1) return; // Slight padding because trees aren't included in the max height calculations

    
    int worldYHalf = chunkPos.y * CHUNK_SIZE;

	// Fill the chunk with blocks based on the heightmap and biome 
    for (int x = 0; x < CHUNK_SIZE; x++) {
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
    Vegetation::placeTreesInChunk(chunkPos.x, chunkPos.y, chunkPos.z, CHUNK_SIZE, seed, blocks, colData);
}

uint32_t Terrain::setRandSeed(void* instancePtr) {
    int local_var;

    // Cast data pointer to uintptr_t
    uintptr_t p1 = reinterpret_cast<uintptr_t>(&local_var);

    // Cast pointer to uintptr_t directly
    uintptr_t p2 = reinterpret_cast<uintptr_t>(instancePtr);

    // Combine the two addresses in some way
    uint32_t combined = Noise::hash(p1, p2, Noise::mix(p1 + p2));

    return combined;
}
