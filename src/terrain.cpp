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


 /*
 * Terrain Generation Notes - 1km = 1000m = 1000 blocks
 *
 |                                                     | Spacing/   | Height/   |
 | Feature                                             | Frecuency  |  Depth    |
 | --------------------------------------------------- | ---------- | --------- |
 | Small landmarks (structures, small/medium caves)    |  0.2-0.5 m |   20–50 m |
 | Villages/camps                                      |     1–3 km |    5-15 m |
 | Large hills or big caves                            | 0.8–1.5 km | 100-250 m |
 | Major mountains                                     |     5–8 km |  0.3-1 km |
 | Major mountain ranges                               |   20–30 km |    1–3 km |
 | Large lakes                                         |     1–5 km |   20-30 m |
 | Inland seas                                         |   10–20 km |   35-55 m |
 *
 * Distances are approximate and can vary based on biome and terrain generation parameters.
 *
 * (?) means that it might be implemented, but isn't strictly necessary
 *
 *
 * Steps to produce terrain:
 *
 * 1. Generate continents and oceans.
 *    * Main noise layer:
 *      - Continental noise: Very low frequency (~0.00002-0.00005)
 *    * Controls:
 *      - Land vs ocean
 *      - Coastline shape
 *      - Inland seas
 *      - Large islands
 *    * Noise generator:
 *      - OpenSimplex2
 *    * Pseudo-code:
 *      - continent = OpenSimplex2(...)
 *      - if continent < seaLevel:
 *      -     Ocean
 *      - else:
 *      -     Land
 *
 * 2. Generate tectonic activity.
 *    * Controls:
 *      - Mountain range probability
 *      - Volcanic regions
 *      - Ore richness
 *      - Future geology
 *    * Noise generator:
 *      - OpenSimplex2 (very low frequency)
 *    * Pseudo-code:
 *      - tectonics = OpenSimplex2(...)
 *
 * 3. Generate mountain range mask.
 *    * Controls:
 *      - Large mountain chains
 *      - Major valleys
 *    * Noise generator:
 *      - Ridged Noise
 *    * Pseudo-code:
 *      - rangeMask = RidgedNoise(...)
 *      - rangeMask *= tectonics;
 *
 * 4. Generate base terrain.
 *    * Main noise layers:
 *      - Regional
 *      - Local
 *      - Terrain
 *      - Detail
 *      - Micro
 *    * Noise Wavelength:
 *      - Regional:  10     -  50    km
 *      - Local:      2     -  10    km
 *      - Terrain:    0.2   -   2    km
 *      - Detail:     0.02  -   0.2  km
 *      - Micro:      0.001 -   0.02 km
 *    * Noise generators:
 *      - Regional -> Ridged
 *      - Local -> Ridged FBM
 *      - Terrain -> FBM OpenSimplex2
 *      - Detail -> Cellular + FBM
 *      - Micro -> Value / White
 *    * Pseudo-code:
 *      - mountains = rangeMask * RidgedFBM(...)
 *      - terrain = FBM(...)
 *      - detail = Cellular(...)
 *      - micro = ValueNoise(...)
 *      - height = continent + mountains + terrain + detail + micro;
 *
 * 5. Apply domain warping.
 *    * Controls:
 *      - Natural terrain flow
 *      - Twisting valleys
 *      - Curved ridges
 *    * Notes:
 *      - Warp everything except continental noise.
 *    * Pseudo-code:
 *      - x2 = x + warpNoise(...) * 300;
 *      - y2 = y + warpNoise2(...) * 300;
 *      - height = Noise(x2, y2);
 *
 * 6. Apply erosion (optional but highly recommended).
 *    * Controls:
 *      - River valleys
 *      - Sediment
 *      - Natural mountain shapes
 *    * Types:
 *      - Hydraulic erosion
 *      - Thermal erosion
 *
 * 7. Generate rivers.
 *    * Controls:
 *      - River network
 *      - Lakes
 *    * Notes:
 *      - Rivers should follow the heightmap, not random noise.
 *      - Flow always moves downhill.
 *
 * 8. Generate biome parameters.
 *
 *    a. Elevation
 *      * Controls:
 *        - Mountains
 *        - Plains
 *        - Valleys
 *        - Snow line
 *      * Pseudo-code:
 *        - elevation = normalize(height);
 *
 *    b. Continentality
 *      * Controls:
 *        - Annual temperature variation
 *        - Overall humidity
 *      * Notes:
 *        - High continentality = deep inland
 *        - Low continentality = coastline
 *      * Pseudo-code:
 *        - d = distanceToNearestOcean(...) - continentality = min(1, d / averageDistanceToOcean);
 *
 *    c. Temperature
 *      * Controls:
 *        - Snow
 *        - Tundra
 *        - Forest type
 *        - Jungle
 *      * Notes:
 *        - Add contributions instead of multiplying.
 *      * Pseudo-code:
 *        - temperature = latitudeTemperature + climateNoise - (elevation * 0.4) - (continentality * 0.1);
 *
 *    d. River Influence
 *      * Controls:
 *        - Greener terrain
 *        - More wildlife
 *        - Settlement suitability
 *      * Pseudo-code:
 *        - d = distanceToNearestRiver(...) - riverInfluence = max(0, 1 - d / averageRiverDistance);
 *
 *    e. Humidity
 *      * Controls:
 *        - Forests
 *        - Deserts
 *        - Grasslands
 *        - Jungles
 *      * Pseudo-code:
 *        - humidity = humidityNoise + riverInfluence - continentality - rainShadow;
 *
 *    f. Fertility
 *      * Controls:
 *        - Vegetation density
 *      * Pseudo-code:
 *        - fertility = humidity + riverInfluence - slope - elevation;
 *
 *    g. Geology
 *      * Controls:
 *        - Bedrock type
 *        - Cave generation
 *        - Rock appearance
 *        - Ore distribution
 *      * Notes:
 *        - Geology should NOT depend on climate.
 *      * Noise generator:
 *        - Very low-frequency OpenSimplex2
 *      * Pseudo-code:
 *        - geologyNoise = OpenSimplex2(...)
 *
 *        - if geologyNoise < 0.25:
 *        -     Granite
 *        - else if geologyNoise < 0.50:
 *        -     Limestone
 *        - else if geologyNoise < 0.75:
 *        -     Basalt
 *        - else:
 *        -     Sandstone
 *
 *        // Tectonic activity can bias probabilities:
 *        // High tectonics -> more basalt/granite
 *        // Low tectonics -> more limestone/sandstone
 *
 *    h. Slope
 *      * Controls:
 *        - Grass
 *        - Forest
 *        - Cliffs
 *        - Bare rock
 *      * Pseudo-code:
 *        - slope = length(gradient(height));
 */



int Terrain::totalChunksGenerated = 0; // Initialize static member variable

// Procedural generation of chunk blocks
void Terrain::generate(const ChunkCoord& chunkPos, const int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE])
{
	/* Time profiling of terrain generation:
    6% cache
    31% block filling
    63% tree placement
    */
    totalChunksGenerated++; // Increment the chunk counter

    if (chunkPos.y < 0) return; // No chunks under negative chunkPos

    // Retrieve column data (cached shared_ptr)
	auto colData = ColumnCache::getOrGenerateColumn(chunkPos.x, chunkPos.z, seed);

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

            int worldX = chunkPos.x * CHUNK_SIZE + x;
            int worldZ = chunkPos.z * CHUNK_SIZE + z; // Still needed for hash calculation below

            for (int y = 0; y < CHUNK_SIZE; y++) {
                int worldY = worldYHalf + y;
                _Block& b = blocks[x][y][z];
                
                if (worldY < height - 2)
                    b.setValues(BlockType::STONE, 0, 0);      // stone
                else if (worldY <= height - 2)
                    b.setValues(BlockType::DIRT, 0, 0);      // dirt
                else if (worldY == height - 1)
                    b.setValues(BlockType::GRASS, 0, 0);      // grass or sand
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
