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



// Terrain foliage parameters
float TREE_DENSITY_MULTIPLIER = 1.0f;   // Global tree density multiplier

std::unordered_map<uint64_t, std::shared_ptr<HeightField::ColumnData>> Terrain::columnCache;
std::mutex Terrain::cacheMutex;


void Terrain::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    columnCache.clear();
}

// Return shared_ptr to avoid large copies and ensure pointer stability
std::shared_ptr<HeightField::ColumnData> Terrain::getOrGenerateColumn(int chunkX, int chunkZ, int seed) {
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

// Procedural generation of chunk blocks
void Terrain::generate( const ChunkCoord& chunkPos, const int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE])
{
    if (chunkPos.y < 0) return; // No chunks under negative chunkPos

    // Retrieve column data (cached shared_ptr)
    auto colData = getOrGenerateColumn(chunkPos.x, chunkPos.z, seed);
    
    // The maximum height in this chunk column in chunk-space
    int maxBlockYInChunkPOS = Noise::floorDiv(colData->maxHeight, CHUNK_SIZE);

    // The chunk is above the highest point in the column - chunk will be empty
    if (chunkPos.y > maxBlockYInChunkPOS + 1) return; // Slight padding because trees aren't included in the max height calculations

    
    int worldYHalf = chunkPos.y * CHUNK_SIZE;

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
    placeTreesInChunk(chunkPos.x, chunkPos.y, chunkPos.z, CHUNK_SIZE, seed, blocks, colData);
}


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
void Terrain::placeTreesInChunk( int chunkX, int chunkY, int chunkZ, int chunkSize, int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE], const std::shared_ptr<HeightField::ColumnData>& centerColData)
{
    // Iterate 3x3 neighborhood (including center)
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            
            int nx = chunkX + dx;
            int nz = chunkZ + dz;

            // Use passed pointer for center, fetch for neighbors (efficient cache)
            auto col = (dx == 0 && dz == 0) ? centerColData : getOrGenerateColumn(nx, nz, seed);
            
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
void Terrain::placeTreeAt(int x, int y, int z, uint32_t& rng, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE])
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
                if ( dx == 0 && dz == 0 && dy <= 0) { blocks[cx][cy][cz].setValues(BlockType::WOOD, 0, 0); continue; }
				if (cy <= y + 1) continue;

				// Only place leaves if the block is currently air
                if (blocks[cx][cy][cz].getType() == BlockType::AIR) blocks[cx][cy][cz].setValues(BlockType::LEAVES, 0, 0);
            }
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

void Terrain::writeChunkHeightmapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename)
{
    if (chunkCountX <= 0 || chunkCountZ <= 0 || !filename) return;

    const int width = chunkCountX * chunkSize;
    const int height = chunkCountZ * chunkSize;

    std::vector<uint8_t> image;
    try {
        image.resize(static_cast<size_t>(width) * height * 3);
    } catch (...) {
        return; // Allocation failed
    }

    int minH = INT32_MAX;
    int maxH = INT32_MIN;

    // First pass: sample heights and find min/max
    std::vector<int> heights;
    try {
        heights.resize(static_cast<size_t>(width) * height);
    } catch (...) {
        return;
    }

    // Generate heightmap values - every column is a pixel
    for (int cz = 0; cz < chunkCountZ; cz++) {
        for (int cx = 0; cx < chunkCountX; cx++) {
            // Get chunk column data
            auto colData = getOrGenerateColumn(startChunkX + cx, startChunkZ + cz, seed);

            for (int lz = 0; lz < chunkSize; lz++) {
                for (int lx = 0; lx < chunkSize; lx++) {
                    int h = colData->heightMap[lx][lz];
                    
                    int pixelX = cx * chunkSize + lx;
                    int pixelZ = cz * chunkSize + lz;
                    heights[pixelZ * width + pixelX] = h;

                    if (h < minH) minH = h;
                    if (h > maxH) maxH = h;
                }
            }
        }
    }

    // Prevent divide-by-zero and handle constant height maps
    float invRange = (maxH > minH) ? 1.0f / float(maxH - minH) : 0.0f;

    // Second pass: write pixels
    for (int i = 0; i < width * height; ++i) {
        float normalized = (maxH > minH) ? float(heights[i] - minH) * invRange : 0.0f;
        float t = std::clamp(normalized, 0.0f, 1.0f);

        float hue;
        float saturation = 1.0f;
        float value = 1.0f;

        // Piecewise hue control (degrees)
        if (t < 0.25f) {
            // Blue (240) -> Cyan (180)
            hue = 240.0f - t / 0.25f * 60.0f;
        }
        else if (t < 0.50f) {
            // Cyan (180) -> Green (120)
            hue = 180.0f - (t - 0.25f) / 0.25f * 60.0f;
        }
        else if (t < 0.70f) {
            // Green (120) -> Yellow (60)
            hue = 120.0f - (t - 0.50f) / 0.20f * 60.0f;
        }
        else if (t < 0.85f) {
            // Yellow (60) -> Red (0)
            hue = 60.0f - (t - 0.70f) / 0.15f * 60.0f;
        }
        else {
            // Red -> White (fade saturation)
            hue = 0.0f;
            saturation = 1.0f - (t - 0.85f) / 0.15f;
        }

        // HSV -> RGB
        float c = value * saturation;
        float x = c * (1.0f - std::fabs(fmod(hue / 60.0f, 2.0f) - 1.0f));
        float m = value - c;

        float rf = 0, gf = 0, bf = 0;

        if (hue < 60) { rf = c; gf = x; bf = 0; }
        else if (hue < 120) { rf = x; gf = c; bf = 0; }
        else if (hue < 180) { rf = 0; gf = c; bf = x; }
        else { rf = 0; gf = x; bf = c; }

        uint8_t r = static_cast<uint8_t>((rf + m) * 255.0f);
        uint8_t g = static_cast<uint8_t>((gf + m) * 255.0f);
        uint8_t b = static_cast<uint8_t>((bf + m) * 255.0f);

        image[i * 3 + 0] = r; // R
        image[i * 3 + 1] = g; // G
        image[i * 3 + 2] = b; // B
    }

    stbi_write_png(filename, width, height, 3, image.data(), width * 3);
}

void Terrain::writeChunkBiomemapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename)
{
    if (chunkCountX <= 0 || chunkCountZ <= 0 || !filename) return;

    const int width = chunkCountX;
    const int height = chunkCountZ;

    std::vector<uint8_t> image;
    try {
        image.resize(static_cast<size_t>(width) * height * 3);
    }
    catch (...) {
        return; // Allocation failed
    }

   
    // First pass: sample heights and find min/max
    std::vector<Biome::BiomeType> biomes;
    try {
        biomes.resize(static_cast<size_t>(width) * height);
    }
    catch (...) {
        return;
    }

	int minSize = std::min(chunkCountX, chunkCountZ);
    auto startTime = std::chrono::high_resolution_clock::now();

	Voronoi::initVoronoi(seed, static_cast<int>(minSize / 4.f), minSize, static_cast<float>(startChunkX), static_cast<float>(startChunkZ));
    for (int cz = 0; cz < chunkCountZ; ++cz) {
        for (int cx = 0; cx < chunkCountX; ++cx) {
			int bx = startChunkX + cx;
            int bz = startChunkZ + cz;
			biomes[cz * width + cx] = Voronoi::sampleBiomeCell(bx, bz, seed);
        }   
    }
    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    std::cout << "Biome map generation took: " << elapsed.count() << " seconds for " << width << "x" << height << " cells.\n";

    
    // Second pass: write pixels
    for (int i = 0; i < width * height; ++i) {
        
        uint8_t r;
        uint8_t g;
        uint8_t b;

        switch (biomes[i]) {
        case Biome::BiomeType::None:
            r = 0; g = 0; b = 0; // Black
			break;
        case Biome::BiomeType::Ocean:
            r = 0; g = 0; b = 128; // Dark Blue
            break;
        case Biome::BiomeType::WarmOcean:
            r = 0; g = 0; b = 255; // Blue
            break;
        case Biome::BiomeType::ArticOcean:
            r = 128; g = 128; b = 255; // Light Blue
            break;
        case Biome::BiomeType::Desert:
            r = 237; g = 201; b = 175; // Sandy
            break;
        case Biome::BiomeType::Savanna:
            r = 189; g = 183; b = 107; // Khaki
            break;
        case Biome::BiomeType::Jungle:
            r = 0; g = 100; b = 0; // Dark green
            break;
        case Biome::BiomeType::Plains:
            r = 124; g = 252; b = 0; // Lawn Green
            break;
        case Biome::BiomeType::Woodland:
            r = 107; g = 142; b = 35; // Olive green for distinction
            break;
        case Biome::BiomeType::Forest:
            r = 34; g = 139; b = 34; // Forest Green
            break;
        case Biome::BiomeType::Tundra:
            r = 176; g = 196; b = 222; // Light Steel Blue
            break;
        case Biome::BiomeType::SnowyTaiga:
            r = 255; g = 250; b = 250; // Snow
            break;
        case Biome::BiomeType::Mountains:
            r = 139; g = 137; b = 137; // Light Gray
            break;
        case Biome::BiomeType::Badlands:
            r = 210; g = 105; b = 30; // Chocolate
            break;
        case Biome::BiomeType::Volcano:
            r = 178; g = 34; b = 34; // Firebrick
            break;
        default:
            r = 0; g = 0; b = 0; // Black for unknown
        }


        image[i * 3 + 0] = r; // R
        image[i * 3 + 1] = g; // G
        image[i * 3 + 2] = b; // B
    }

    stbi_write_png(filename, width, height, 3, image.data(), width * 3);
}