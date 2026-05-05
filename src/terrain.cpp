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

#include "terrain.hpp"
#include <cmath>

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

// Terrain erosion parameters
constexpr int TALUS = 3;        // max allowed height difference
constexpr int EROSION_PASSES = 4;

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

// Limits
constexpr float MAX_TERRAIN_HEIGHT = 512.0f;        // Maximum terrain height in blocks
constexpr float BASE_TERRAIN_HEIGHT = 48.0f;        // Base terrain height in blocks
constexpr float MIN_TERRAIN_HEIGHT = 32.0f;         // Minimum terrain height in blocks

// Terrain foliage parameters
float TREE_DENSITY_MULTIPLIER = 1.0f;   // Global tree density multiplier

std::vector<VoronoiSite> Terrain::voronoiSites;
VoronoiSpatialGrid Terrain::voronoiGrid;
std::unordered_map<uint64_t, std::shared_ptr<Terrain::ColumnData>> Terrain::columnCache;
std::mutex Terrain::cacheMutex;

/*
 * Hash mixing functions for procedural generation.
 * Provides deterministic pseudo-random values from coordinates and seeds.
 */
inline uint32_t Terrain::mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352D;
    x ^= x >> 15;
    x *= 0x846BA67B;
    x ^= x >> 16;
    return x;
}
inline uint32_t Terrain::hash(int x, int z, int seed) {
    uint64_t h = static_cast<uint64_t>(x) * 0x165667B5ULL +
        static_cast<uint64_t>(z) * 0x27D4EB2BULL +
        static_cast<uint64_t>(seed) * 0x48FC803BULL;
    h = (h ^ (h >> 13)) * 0x4BF19F5DULL;
    return static_cast<uint32_t>(h ^ (h >> 16));
}
inline uint32_t Terrain::chunkSeed(const ChunkCoord& c, int seed) {
    uint32_t h = 0;
    h ^= mix(static_cast<uint32_t>(c.x));
    h ^= mix(static_cast<uint32_t>(c.y) + 0x9e3779b1);
    h ^= mix(static_cast<uint32_t>(c.z) + 0x85F99D69);
    h ^= mix(static_cast<uint32_t>(seed));
    return mix(h);
}
inline uint32_t Terrain::rand_u32(uint32_t baseSeed, uint32_t index) {
    return mix(baseSeed + index * 0x9e3779b1);
} 
inline float rand01(uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (state & 0xFFFFFF) / float(0x1000000);
}
inline int Terrain::floorDiv(int a, int b) {
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}
inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}
inline float remap01(float v) {
    return v * 0.5f + 0.5f; // [-1,1] -> [0,1]
}
inline float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}
inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}
inline float slopeLimit(float delta, float maxSlope) {
    return std::clamp(delta, -maxSlope, maxSlope);
}
inline float slopeMagnitude(float h, float hx, float hz) {
    float dx = h - hx;
    float dz = h - hz;
    return std::sqrt(dx * dx + dz * dz);
}
inline float compressHeight(float h, float center, float maxDelta) {
    float d = h - center;

    if (std::abs(d) <= maxDelta)
        return h;

    float sign = (d > 0.0f) ? 1.0f : -1.0f;
    float excess = std::abs(d) - maxDelta;

    // Nonlinear compression
    excess = std::sqrt(excess);

    return center + sign * (maxDelta + excess);
}


/*
 * Determine biome from climate parameters.
 * Uses temperature, moisture, weirdness (mountains), and continent values.
 */
Biome computeBiomeFromClimate(float temp, float moisture, float weird, float continent) {
    if (continent < OCEAN_THRESHOLD) {
        if (temp > 0.5f) return Biome::WarmOcean;
        else if (temp < 0.3) return Biome::ArticOcean;
		else return Biome::Ocean;
	}
    if (weird > 0.85f) {
        if (moisture < 0.25f && temp > 0.4) return Biome::Badlands;
        else if (weird >= 0.99f && temp >= 0.9f) return Biome::Volcano;
        else return Biome::Mountains;
    }
    if (temp > 0.7f) {
        if (moisture < 0.4f) return Biome::Desert;
        else if (moisture < 0.6f) return Biome::Savanna;
        else return Biome::Jungle;
    }
    else if (temp > 0.3f && temp < 0.7f) {
        if (moisture < 0.3f) return Biome::Plains;
        else if (moisture < 0.55f) return Biome::Woodland;
        else return Biome::Forest;
    }
    else {  // temp <= 0.3f
        if (moisture < 0.5f) return Biome::Tundra;
        else return Biome::SnowyTaiga;
    }
	return Biome::None;  // Fallback, shows when something goes wrong
}

// Permutation table constants (fixed, no need to randomize)
static const int8_t perm[] = {
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
    184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
    222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180
};

// Helper functions
static inline float grad2(int hash, float x, float y) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : 0);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static inline int fastFloor(float x) {
    return (int)(x < 0 ? x - 1 : x);
}

/*
 * 2D OpenSimplex2 noise implementation.
 * Returns values in approximately [-1, 1] range.
 * Used as the foundation for all terrain noise layers.
 */
float Terrain::openSimplex2(float x, float y, int seedOffset)
{
    // Skewing and unskewing factors for 2D
    const float F2 = 0.5f * (std::sqrt(3.0f) - 1.0f);
    const float G2 = (3.0f - std::sqrt(3.0f)) / 6.0f;

    float s = (x + y) * F2;
    int i = fastFloor(x + s);
    int j = fastFloor(y + s);

    float t = (i + j) * G2;
    float X0 = i - t;
    float Y0 = j - t;
    float x0 = x - X0;
    float y0 = y - Y0;

    int i1, j1;
    if (x0 > y0) { i1 = 1; j1 = 0; }
    else { i1 = 0; j1 = 1; }

    float x1 = x0 - i1 + G2;
    float y1 = y0 - j1 + G2;
    float x2 = x0 - 1.0f + 2.0f * G2;
    float y2 = y0 - 1.0f + 2.0f * G2;

    // Hash lookup
    auto hash = [&](int px, int py) {
        uint32_t h = chunkSeed({ px, 0, py }, seedOffset);
        h = rand_u32(h, 0);
        return (h >> 24) & 255;  // 8-bit permutation index
        };

    int gi0 = hash(i, j);
    int gi1 = hash(i + i1, j + j1);
    int gi2 = hash(i + 1, j + 1);

    float t0 = 0.5f - x0 * x0 - y0 * y0;
    float n0 = t0 < 0 ? 0 : (t0 * t0 * t0 * t0 * grad2(perm[gi0], x0, y0));

    float t1 = 0.5f - x1 * x1 - y1 * y1;
    float n1 = t1 < 0 ? 0 : (t1 * t1 * t1 * t1 * grad2(perm[gi1], x1, y1));

    float t2 = 0.5f - x2 * x2 - y2 * y2;
    float n2 = t2 < 0 ? 0 : (t2 * t2 * t2 * t2 * grad2(perm[gi2], x2, y2));

    return 70.0f * (n0 + n1 + n2);  // ≈ [-1,1]
}

/*
 * Generate terrain height at a world coordinate.
 * Combines biome parameters with multi-octave noise for varied terrain.
 */
int Terrain::generateHeight(int worldX, int worldZ, const BiomeParams& biome, int seed)
{
    
    // High peaks, but changes slowly
    float continentNoise = remap01( openSimplex2(worldX * 0.0008f, worldZ * 0.0008f, seed + 11) );

    // Push oceans down hard
    float continent = smoothstep(continentNoise);
    continent = continent * continent; // bias toward oceans and high mountains

    // Main terrain shape
    float baseNoise = remap01( openSimplex2(worldX * 0.004f, worldZ * 0.004f, seed + 23) );

    float height = biome.baseHeight + baseNoise * biome.amplitude * 0.35f;
    height *= continent;

	// Do extra continent shaping for mountains
    if (biome.mountainStrength > 0.0f) {
        float mountainMask = remap01( openSimplex2(worldX * 0.0015f, worldZ * 0.0015f, seed + 47) );

        mountainMask = smoothstep(mountainMask);
        mountainMask *= biome.mountainStrength;

        // Hard mountain cap (hardcoded for now)
        const float maxMountainAdd = MAX_TERRAIN_HEIGHT; // biome.amplitude;

        // Height-relative taper
        float relative = (height - biome.baseHeight) / maxMountainAdd;
        relative = clamp01(relative);

        // Aggressive taper (prevents vertical walls)
        float taper = 1.0f - (relative * relative * relative);

        float mountainAdd = mountainMask * maxMountainAdd * taper;

        height += mountainAdd;
    }

	// Fine detail (changes every 50 blocks)
    float detail = openSimplex2(worldX * 0.02f, worldZ * 0.02f, seed + 91) * 2.5f;

    height += detail;

    height += BASE_TERRAIN_HEIGHT;

	height += ridgedNoise(worldX * 0.02f, worldZ * 0.019f, seed); // Slight ridged noise for sharpness (goes faster in the Z direction)

	// Clamp to valid and reasonable range
    height = (int)std::clamp(height, MIN_TERRAIN_HEIGHT, MAX_TERRAIN_HEIGHT);

    return height;
}


/*
 * Ridge noise function for mountain generation.
 * Inverts absolute value of noise to create sharp ridges.
 */
float Terrain::ridge(float n) {
    // Classic ridged multifractal style - sharp ridges, good for mountains / weirdness
    n = std::abs(n);
    n = 1.0f - n;           // invert
    n = n * n;              // sharpen (optional extra power)
    return n;
}

float Terrain::terrace(float h, float step, float strength)
{
    float base = floor(h / step) * step;
    float frac = (h - base) / step;

    // Smooth transition between steps
    frac = frac * frac * (3.0f - 2.0f * frac);

    return base + frac * step * strength;
}

float Terrain::heightNoise2D(int wx, int wz, int seed)
{
    float sum = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;

    for (int i = 0; i < 4; ++i) {
        float n = openSimplex2(wx * freq, wz * freq, seed + i * 131);
        sum += n * amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }

    float normalized = sum * 1.4f;  // Adjusted boost for fewer octaves (test empirically)
    normalized = (normalized + 1.0f) / 2.0f;
    normalized = std::clamp(normalized, 0.0f, 1.0f);

    return normalized;
}

float Terrain::heightNoise(int wx, int wz, int seed)
{
    const int SCALE = 32;

    int x0 = floorDiv(wx, SCALE);
    int z0 = floorDiv(wz, SCALE);
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    float fx = float(wx - x0 * SCALE) / SCALE;
    float fz = float(wz - z0 * SCALE) / SCALE;

    // Smooth fade function (much less grid-like than linear interp)
    float u = fx * fx * (3.0f - 2.0f * fx);   // hermite / smoothstep
    float v = fz * fz * (3.0f - 2.0f * fz);

    auto sample = [&](int x, int z) {
        return float(rand_u32(chunkSeed({ x,0,z }, seed), 0) & 0xFFFF) / 65535.0f;
        };

    float a = sample(x0, z0);
    float b = sample(x1, z0);
    float c = sample(x0, z1);
    float d = sample(x1, z1);

    return lerp(lerp(a, b, u), lerp(c, d, u), v);
}

void Terrain::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex);
    columnCache.clear();
}

void Terrain::capRidges(ColumnData& heightmap, ColumnData* west, ColumnData* east, ColumnData* north, ColumnData* south)
{
    const float RIDGE_CAP = 8.0f;
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            float h = (float)heightmap.heightMap[x][z];

            // Check 4 directions for hard ridge capping
            struct { int dx, dz; ColumnData* col; } neighbors[4] = {
                { 1, 0,  (x < CHUNK_SIZE - 1) ? &heightmap : east },
                {-1, 0,  (x > 0) ? &heightmap : west },
                { 0, 1,  (z < CHUNK_SIZE - 1) ? &heightmap : south },
                { 0,-1,  (z > 0) ? &heightmap : north }
            };

            for (int i = 0; i < 4; i++) {
                if (!neighbors[i].col) continue;

                int nx = (x + neighbors[i].dx + CHUNK_SIZE) % CHUNK_SIZE;
                int nz = (z + neighbors[i].dz + CHUNK_SIZE) % CHUNK_SIZE;
                
                float nh = (float)neighbors[i].col->heightMap[nx][nz];
                h = nh + slopeLimit(h - nh, RIDGE_CAP);
            }
            heightmap.heightMap[x][z] = (uint16_t)h;
        }
    }
}

void Terrain::thermalErosion(ColumnData& heightmap, ColumnData* west, ColumnData* east, ColumnData* north, ColumnData* south)
{
    for (int pass = 0; pass < EROSION_PASSES; pass++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                int current = heightmap.heightMap[x][z];

                // Check 4 directions: East, West, South, North
                struct { int dx, dz; ColumnData* col; bool isInternal; } neighbors[4] = {
                    { 1, 0,  (x < CHUNK_SIZE - 1) ? &heightmap : east,  (x < CHUNK_SIZE - 1) },
                    {-1, 0,  (x > 0) ? &heightmap : west,               (x > 0) },
                    { 0, 1,  (z < CHUNK_SIZE - 1) ? &heightmap : south, (z < CHUNK_SIZE - 1) },
                    { 0,-1,  (z > 0) ? &heightmap : north,              (z > 0) }
                };

                for (int i = 0; i < 4; i++) {
                    if (!neighbors[i].col) continue;

                    int nx = (x + neighbors[i].dx + CHUNK_SIZE) % CHUNK_SIZE;
                    int nz = (z + neighbors[i].dz + CHUNK_SIZE) % CHUNK_SIZE;
                    
                    uint16_t* nPtr = &neighbors[i].col->heightMap[nx][nz];
                    int diff = current - *nPtr;

                    if (diff > TALUS) {
                        int move = (diff - TALUS) / 2;
                        heightmap.heightMap[x][z] -= move;
                        current -= move; // Update current to reflect change for next neighbor
                        
                        // Only mutate if the neighbor is within the same column
                        if (neighbors[i].isInternal) {
                            *nPtr += move;
                        }
                    }
                }
            }
        }
    }
}


// Return shared_ptr to avoid large copies and ensure pointer stability
std::shared_ptr<Terrain::ColumnData> Terrain::getOrGenerateColumn(int chunkX, int chunkZ, int seed) {
    uint64_t key = packCoords(chunkX, chunkZ);

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = columnCache.find(key);
        if (it != columnCache.end()) {
            return it->second;
        }
    }

    // Generate if not found - allocate on heap immediately
    auto data = std::make_shared<ColumnData>();
    
    Biome chunkBiome = sampleBiomeCell(chunkX, chunkZ, seed);
	BiomeParams biomeParams = sampleBlendedBiomeParams(chunkX * CHUNK_SIZE + CHUNK_SIZE / 2, chunkZ * CHUNK_SIZE + CHUNK_SIZE / 2, seed);
    data->biome = chunkBiome; 

    // Retrieve neighbors from cache to handle slope-aware attenuation across chunk boundaries
    std::shared_ptr<ColumnData> westCol = nullptr, eastCol = nullptr, northCol = nullptr, southCol = nullptr;
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
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            data->heightMap[x][z] = (uint16_t)generateHeight(chunkX * CHUNK_SIZE + x, chunkZ * CHUNK_SIZE + z, biomeParams, seed);
        }
    }

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
            else hx = (float)generateHeight(worldX - 1, worldZ, biomeParams, seed);

            // East neighbor (x+1)
            if (x < CHUNK_SIZE - 1) he = (float)data->heightMap[x + 1][z];
            else if (eastCol) he = (float)eastCol->heightMap[0][z];
            else he = (float)generateHeight(worldX + 1, worldZ, biomeParams, seed);

            // North neighbor (z-1)
            if (z > 0) hz = (float)data->heightMap[x][z - 1];
            else if (northCol) hz = (float)northCol->heightMap[x][CHUNK_SIZE - 1];
            else hz = (float)generateHeight(worldX, worldZ - 1, biomeParams, seed);

            // South neighbor (z+1)
            if (z < CHUNK_SIZE - 1) hs = (float)data->heightMap[x][z + 1];
            else if (southCol) hs = (float)southCol->heightMap[x][0];
            else hs = (float)generateHeight(worldX, worldZ + 1, biomeParams, seed);

            // Hard ridge cap
            float slope = slopeMagnitude(h, hx, hz);
            const float RIDGE_MAX = 8.0f;
            if (slope > RIDGE_MAX) {
                float excess = slope - RIDGE_MAX;
                h -= excess * 0.75f;
            }

            // Sequential slope limiting
            h = hx + slopeLimit(h - hx, 3.0f);
            h = he + slopeLimit(h - he, 3.0f);
            h = hz + slopeLimit(h - hz, 3.0f);
            h = hs + slopeLimit(h - hs, 3.0f);

            data->heightMap[x][z] = (uint16_t)h;
        }
    }


    // Step 3: Hard ridge cap at 8 blocks
    capRidges(*data, westCol.get(), eastCol.get(), northCol.get(), southCol.get());

    // Step 4: Apply thermal erosion with boundary awareness
    thermalErosion(*data, westCol.get(), eastCol.get(), northCol.get(), southCol.get());

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
    
    int worldYHalf = chunkPos.y * CHUNK_SIZE;

    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {

            int height = colData->heightMap[x][z];
            Biome biome = colData->biome;

            int worldX = chunkPos.x * CHUNK_SIZE + x;
            int worldZ = chunkPos.z * CHUNK_SIZE + z; // Still needed for hash calculation below

            for (int y = 0; y < CHUNK_SIZE; y++) {
                int worldY = worldYHalf + y;
                _Block& b = blocks[x][y][z];
                
                if (worldY == 0 || (worldY == 1 && hash(worldX, worldZ, seed) % 3 == 0) || (worldY == 2 && hash(worldX, worldZ, seed) % 5 == 0))
                    b.setValues(BlockType::BEDROCK, 0, 0);      // bedrock
                else if (worldY < height - 2)
                    b.setValues(BlockType::STONE, 0, 0);      // stone
                else if (worldY < height - 1)
                    b.setValues(((int)biome <= 3 ? BlockType::SAND : (biome == Biome::Mountains ? BlockType::STONE : BlockType::DIRT)), 0, 0);      // dirt
                else if (worldY == height - 1)
                    b.setValues(((int)biome <= 3 ? BlockType::SAND : (biome == Biome::Mountains ? BlockType::STONE : BlockType::GRASS)), 0, 0);      // grass or sand
                else
                    b.setValues(BlockType::AIR, 0, 0);      // air
            }
        }
    }
    placeTreesInChunk(chunkPos.x, chunkPos.y, chunkPos.z, CHUNK_SIZE, seed, blocks, colData);
}
// Simulate neighbor trees to handle overhangs
void Terrain::placeTreesInChunk( int chunkX, int chunkY, int chunkZ, int chunkSize, int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE], const std::shared_ptr<ColumnData>& centerColData)
{
    // Iterate 3x3 neighborhood (including center)
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            
            int nx = chunkX + dx;
            int nz = chunkZ + dz;

            // Use passed pointer for center, fetch for neighbors (efficient cache)
            auto col = (dx == 0 && dz == 0) ? centerColData : getOrGenerateColumn(nx, nz, seed);
            
            // Re-generate deterministic base seed for this chunk
            uint32_t chunkBaseRNG = hash(nx, nz, seed);

            // Calculate tree count for this neighbor chunk
            int centerWorldX = nx * chunkSize + chunkSize / 2;
            int centerWorldZ = nz * chunkSize + chunkSize / 2;
            BiomeParams biome = sampleBlendedBiomeParams(centerWorldX, centerWorldZ, seed);

            if (biome.treeDensity <= 0.0f) continue;

            float cluster = heightNoise2D(centerWorldX * 0.01f, centerWorldZ * 0.01f, seed);
            cluster = (cluster + 1.0f) * 0.6f;
            
            // Use chunkBaseRNG consistently for treeCount
            uint32_t countRNG = chunkBaseRNG;
            int treeCount = int(biome.treeDensity * cluster + rand01(countRNG) * TREE_DENSITY_MULTIPLIER);

            for (int i = 0; i < treeCount; ++i)
            {
                // CRITICAL: Each tree slot MUST have its own deterministic seed
                // This ensures coordinate picking at i=1 doesn't depend on whether i=0 was drawn.
                uint32_t treeRNG = hash(nx, nz, seed + i + 1);

                // Local coordinate IN THE NEIGHBOR CHUNK
                int lx = int(rand01(treeRNG) * chunkSize);
                int lz = int(rand01(treeRNG) * chunkSize);

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
void Terrain::placeTreeAt(int x, int y, int z, uint32_t& rng, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE])
{
    // No early return for X/Z
    // We might need to draw leaves even if trunk is outside this chunk.

	int trunkHeight = 2 + int(rand01(rng) * 3); // 2-4 blocks tall

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
    uint32_t combined = hash(p1, p2, mix(p1 + p2));

    return combined;
}

/*
 * Sample blended biome parameters at a world position.
 * Performs bilinear interpolation between 4 neighboring biome cells.
 */
BiomeParams Terrain::sampleBlendedBiomeParams(int worldX, int worldZ, int seed)
{
    int bx = floorDiv(worldX, BIOME_CELL_SIZE);
    int bz = floorDiv(worldZ, BIOME_CELL_SIZE);

    float fx = float(worldX - bx * BIOME_CELL_SIZE) / BIOME_CELL_SIZE;
    float fz = float(worldZ - bz * BIOME_CELL_SIZE) / BIOME_CELL_SIZE;

    Biome b00 = sampleBiomeCell(bx, bz, seed);
    Biome b10 = sampleBiomeCell(bx + 1, bz, seed);
    Biome b01 = sampleBiomeCell(bx, bz + 1, seed);
    Biome b11 = sampleBiomeCell(bx + 1, bz + 1, seed);

    BiomeParams p00 = getParams(b00);
    BiomeParams p10 = getParams(b10);
    BiomeParams p01 = getParams(b01);
    BiomeParams p11 = getParams(b11);

    // Bilinear interpolation weights
    float w00 = (1 - fx) * (1 - fz);
    float w10 = fx * (1 - fz);
    float w01 = (1 - fx) * fz;
    float w11 = fx * fz;

	BiomeParams result;
	result.amplitude = p00.amplitude * w00 + p10.amplitude * w10 + p01.amplitude * w01 + p11.amplitude * w11;
	result.baseHeight = p00.baseHeight * w00 + p10.baseHeight * w10 + p01.baseHeight * w01 + p11.baseHeight * w11;
	result.mountainStrength = p00.mountainStrength * w00 + p10.mountainStrength * w10 + p01.mountainStrength * w01 + p11.mountainStrength * w11;
	result.treeDensity = p00.treeDensity * w00 + p10.treeDensity * w10 + p01.treeDensity * w01 + p11.treeDensity * w11;
    result.treeLine = p00.treeLine * w00 + p10.treeLine * w10 + p01.treeLine * w01 + p11.treeLine * w11;

	return result;
}

// Ridged noise function for sharp features [0.f, 1.f]
float Terrain::ridgedNoise(int wx, int wz, int seed)
{
    float n = openSimplex2(static_cast<float>(wx), static_cast<float>(wz), seed);
    n = 1.0f - std::abs(n);   // openSimplex2 is in [-1, 1], so abs(n) is [0, 1]
    return n * n;
}

// Helper: simple fBm using your existing heightNoise2D
float Terrain::fbmContinent(float wx, float wz, int seed, float baseScale)
{
    // 1. Smooth base layer for general landmass shape
    float baseLayer = openSimplex2(wx * baseScale * 0.5f, wz * baseScale * 0.5f, seed + 99) * 0.5f + 0.5f;

    // 2. Valley layer (narrow channels and lakes)
    // Uses absolute value of noise to create valleys at the zero-crossings
    float valleys = std::abs(openSimplex2(wx * baseScale * 1.5f, wz * baseScale * 1.5f, seed + 555));
    valleys = 1.0f - std::clamp(valleys * 3.0f, 0.0f, 1.0f); // Higher intensity at center of valley

    // 3. Island layer (clusters of small landmasses)
    float islands = openSimplex2(wx * 0.02f, wz * 0.02f, seed + 777) * 0.5f + 0.5f;
    islands = std::pow(islands, 4.0f); // Make it sparse

    // 4. Detail ridged layers (roughness)
    float ridgedSum = 0.0f;
    float ridgedAmp = 1.0f;
    float ridgedFreq = baseScale;
    float weight = 1.0f;

    for (int i = 0; i < 5; i++) {
        float signal = ridgedNoise(static_cast<int>(wx * ridgedFreq), static_cast<int>(wz * ridgedFreq), seed + i * 131);
        signal *= weight;
        weight = std::clamp(signal * 2.0f, 0.0f, 1.0f);
        ridgedSum += signal * ridgedAmp;
        ridgedAmp *= 0.5f;
        ridgedFreq *= 2.1f;
    }

    // Combine: valleys erode land, islands add to sea
    float combined = (baseLayer * 0.5f + ridgedSum * 0.5f);
    combined -= valleys * 0.4f; // Carve valleys
    combined += islands * 0.3f; // Spawn islands

    return combined * 1.25f;
}
// Helper: fBm for climate parameters (similar to fbmContinent)
float Terrain::fbmClimate(float wx, float wz, int seed, float baseFreq)
{
    const int octaves = 4;
    const float lacunarity = 2.3f;
    const float gain = 0.51f;

    float sum = 0.0f;
    float amp = 1.0f;
    float freq = baseFreq;

    for (int i = 0; i < octaves; ++i) {
        sum += heightNoise2D(wx * freq, wz * freq, seed + i * 131) * amp;
        amp *= gain;
        freq *= lacunarity;
    }

    float maxPossible = (1.0f - std::pow(gain, octaves)) / (1.0f - gain);
    return sum / maxPossible;
}
float Terrain::fbmWarp(float wx, float wz, int seed, float baseFreq) {
    const int octaves = 4;  // Increased to allow more detail influence
    const float lacunarity = 2.0f;
    const float gain = 0.7f;  // Elevated from 0.6f for stronger high-frequency contribution

    float sum = 0.0f;
    float amp = 1.0f;
    float freq = baseFreq;

    for (int i = 0; i < octaves; ++i) {
        sum += heightNoise2D(wx * freq, wz * freq, seed + i * 131) * amp;
        amp *= gain;
        freq *= lacunarity;
    }

    float maxPossible = (1.0f - std::pow(gain, octaves)) / (1.0f - gain);
    return sum / maxPossible;
}


Biome Terrain::assignRandomBiome(int seed) {
    int rnd = static_cast<int>(heightNoise2D(static_cast<float>(seed), 0.0f, seed) * 14);  // Limit to 14 for defined cases
    switch (rnd % 14) {
    case 0: return Biome::WarmOcean;  // Optional: Bias some to ocean if needed
    case 1: return Biome::ArticOcean;
    case 2: return Biome::Desert;
    case 3: return Biome::Savanna;
    case 4: return Biome::Jungle;
    case 5: return Biome::Plains;
    case 6: return Biome::Woodland;
    case 7: return Biome::Forest;
    case 8: return Biome::Tundra;
    case 9: return Biome::SnowyTaiga;
    case 10: return Biome::Mountains;
    case 11: return Biome::Badlands;
    case 12: return Biome::Volcano;
    case 13: return Biome::Ocean;  // Fallback to a valid biome
    default: return Biome::Plains;  // Safety net, though %14 should prevent this
    }
}

/**
 * Apply domain warping to coordinates.
 * Adds non-linear distortion to reduce grid artifacts in noise.
 */
void Terrain::domainWarp(float& x, float& z, int seed) {
    // Octave 1: Large scale warping
    float dx = openSimplex2(x * WARP_FREQUENCY_LOW, z * WARP_FREQUENCY_LOW, seed + 9001) * WARP_AMPLITUDE_LOW;
    float dz = openSimplex2(x * WARP_FREQUENCY_LOW + 5.2f, z * WARP_FREQUENCY_LOW + 1.3f, seed + 9002) * WARP_AMPLITUDE_LOW;
    
    // Octave 2: High frequency detail ripples
    dx += openSimplex2(x * WARP_FREQUENCY_HIGH, z * WARP_FREQUENCY_HIGH, seed + 9003) * WARP_AMPLITUDE_HIGH;
    dz += openSimplex2(x * WARP_FREQUENCY_HIGH + 2.7f, z * WARP_FREQUENCY_HIGH + 4.9f, seed + 9004) * WARP_AMPLITUDE_HIGH;

    x += dx;
    z += dz;
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
void Terrain::initVoronoi(int seed, int numSites, float mapSize, float startX, float startZ) {
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
        float jitterAmp = spacing * VORONOI_JITTER;
        float sx = baseSx + heightNoise2D(baseSx, baseSz, seed + 1000) * jitterAmp - jitterAmp * 0.5f;
        float sz = baseSz + heightNoise2D(baseSx * 1.1f, baseSz * 0.9f, seed + 1001) * jitterAmp - jitterAmp * 0.5f;

        // Warp sites slightly for less robotic feel
        float wsx = sx, wsz = sz;
        domainWarp(wsx, wsz, seed + 555);

        // Assign climate parameters using multi-frequency noise
        const float MAIN_CLIMATE_SCALE = CLIMATE_FREQUENCY;
        const float WEIRD_SCALE = WEIRD_FREQUENCY;

        // Temperature: Varies with latitude + noise
        float temp = 0.2f + 0.3f * std::sin(wsz * 0.0005f) + 0.58f * (openSimplex2(wsx * MAIN_CLIMATE_SCALE, wsz * MAIN_CLIMATE_SCALE, seed + 731) * 0.5f + 0.5f);
        temp = clamp01(temp);

        // Moisture: Independent noise layer
        float moisture = openSimplex2(wsx * MAIN_CLIMATE_SCALE * 1.35f, wsz * MAIN_CLIMATE_SCALE * 1.35f, seed + 1249) * 0.5f + 0.5f;
        moisture = clamp01(moisture);

        // Weirdness: Ridged noise for mountain/unusual terrain
        float weird_raw = openSimplex2(wsx * WEIRD_SCALE, wsz * WEIRD_SCALE, seed + 3791);
        float weird = ridge(weird_raw);
        weird = clamp01(weird * 1.1f);

        // Continental scale: Determines if area is land or ocean
        float continent = fbmContinent(wsx, wsz, seed + 5000, CONTINENT_FREQUENCY);
        continent = clamp01(continent * CONTINENT_SCALE);

        Biome siteBiome = computeBiomeFromClimate(temp, moisture, weird, continent);
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
Biome Terrain::sampleBiomeCell(int bx, int bz, int seed) {
    float x = static_cast<float>(bx);
    float z = static_cast<float>(bz);

    // Apply domain warping to bridge Voronoi cell boundaries naturally
    float wx = x, wz = z;
    domainWarp(wx, wz, seed);

    // Find nearest Voronoi site using spatial grid
    float minDist = std::numeric_limits<float>::max();
    Biome nearestBiome = Biome::None;

    int gx = static_cast<int>((x - voronoiGrid.startX) / voronoiGrid.cellSize);
    int gz = static_cast<int>((z - voronoiGrid.startZ) / voronoiGrid.cellSize);

    // Check current cell and neighbors
    for (int dz = -2; dz <= 2; ++dz) { // Increased search radius slightly for safety with jitter
        for (int dx = -2; dx <= 2; ++dx) {
            int ngx = gx + dx;
            int ngz = gz + dz;
            if (ngx >= 0 && ngx < voronoiGrid.cols && ngz >= 0 && ngz < voronoiGrid.rows) {
                const auto& cellSites = voronoiGrid.grid[ngz * voronoiGrid.cols + ngx];
                for (size_t siteIdx : cellSites) {
                    const auto& site = voronoiSites[siteIdx];
                    
                    // Add high-frequency jitter for "jagged" boundaries
                    float jx = wx + openSimplex2(wx * 0.15f, wz * 0.15f, seed + 123) * 3.5f;
                    float jz = wz + openSimplex2(wx * 0.15f + 7.7f, wz * 0.15f + 3.3f, seed + 456) * 3.5f;

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
    if (nearestBiome == Biome::None) {
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
    float continent = fbmContinent(wx, wz, seed + 5000, CONTINENT_FREQUENCY);
    continent = clamp01(continent * CONTINENT_SCALE);

    const float CLIMATE_SCALE = CLIMATE_FREQUENCY;
    float temp = 0.2f + 0.3f * std::sin(wz * 0.0005f) + 0.58f * (openSimplex2(wx * CLIMATE_SCALE, wz * CLIMATE_SCALE, seed + 731) * 0.5f + 0.5f);
    temp = clamp01(temp);

    // Dynamic threshold for more natural, noisy coastlines
    float beachNoise = openSimplex2(wx * 0.1f, wz * 0.1f, seed + 888) * BEACH_NOISE_SCALE;
    float dynamicThreshold = OCEAN_THRESHOLD + beachNoise;

    // Decision logic using overrides (e.g. Ocean)
    if (continent < dynamicThreshold) { 
        if (temp > 0.7f) return Biome::WarmOcean;
        else if (temp < 0.3f) return Biome::ArticOcean;
        else return Biome::Ocean;
    }

    // Otherwise, use the nearest Voronoi-assigned biome
    return nearestBiome;
}

/**
 * Get biome parameters (base height, amplitude, mountain strength).
 * These control terrain generation differently for each biome.
 */
BiomeParams Terrain::getParams(Biome b)
{
	// Parameters: baseHeight, amplitude, mountainStrength, treeDensity, treeMaxBaseHeight
    switch (b) {
    case Biome::Ocean:
        return { 8.0f, 4.0f, 0.0f, 0.0f, 0.0f };

    case Biome::WarmOcean:
        return { 12.0f, 5.0f, 0.0f, 0.0f, 0.0f };

    case Biome::ArticOcean:
        return { 6.0f, 3.0f, 0.0f, 0.0f, 0.0f };

    case Biome::Desert:
        return { 62.0f, 4.0f, 0.2f, 0.0f, 0.0f };

    case Biome::Savanna:
        return { 68.0f, 6.0f, 0.1f, 0.6f, 110.0f };

    case Biome::Jungle:
        return { 70.0f, 8.0f, 0.2f, 6.0f, 125.0f };

    case Biome::Plains:
        return { 64.0f, 6.0f, 0.0f, 1.2f, 115.0f };

    case Biome::Woodland:
        return { 65.0f, 6.5f, 0.1f, 2.8f, 120.0f };

    case Biome::Forest:
        return { 66.0f, 7.0f, 0.2f, 4.5f, 120.0f };

    case Biome::Tundra:
        return { 58.0f, 5.0f, 0.0f, 0.3f, 90.0f };

    case Biome::SnowyTaiga:
        return { 54.0f, 4.0f, 0.1f, 2.0f, 95.0f };

    case Biome::Mountains:
        return { 80.0f, 280.0f, 1.0f, 0.25f, 95.0f };

    case Biome::Badlands:
        return { 72.0f, 175.0f, 1.0f, 0.05f, 90.0f };

    case Biome::Volcano:
        return { 90.0f, 280.0f, 1.0f, 0.0f, 0.0f };
    }

    return { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

}

// Gamma function: x^gamma
float gammaCurve(float x, float gamma) {
    return std::pow(x, gamma);
}

// Sigmoid function centered at 0.5 with adjustable steepness
float sigmoidCurve(float x, float steepness = 10.0f) {
    return 1.0f / (1.0f + std::exp(-steepness * (x - 0.5f)));
}
// Normalize three floats so their sum is 1 (if sum > 0)
void normalize3(float& a, float& b, float& c) {
    float sum = a + b + c;
    if (sum > 0.0f) {
        a /= sum;
        b /= sum;
        c /= sum;
    }
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
    std::vector<Biome> biomes;
    try {
        biomes.resize(static_cast<size_t>(width) * height);
    }
    catch (...) {
        return;
    }

	int minSize = std::min(chunkCountX, chunkCountZ);
    auto startTime = std::chrono::high_resolution_clock::now();

	initVoronoi(seed, static_cast<int>(minSize / 4.f), minSize, static_cast<float>(startChunkX), static_cast<float>(startChunkZ));
    for (int cz = 0; cz < chunkCountZ; ++cz) {
        for (int cx = 0; cx < chunkCountX; ++cx) {
			int bx = startChunkX + cx;
            int bz = startChunkZ + cz;
			biomes[cz * width + cx] = sampleBiomeCell(bx, bz, seed);
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
        case Biome::None:
            r = 0; g = 0; b = 0; // Black
			break;
        case Biome::Ocean:
            r = 0; g = 0; b = 128; // Dark Blue
            break;
        case Biome::WarmOcean:
            r = 0; g = 0; b = 255; // Blue
            break;
        case Biome::ArticOcean:
            r = 128; g = 128; b = 255; // Light Blue
            break;
        case Biome::Desert:
            r = 237; g = 201; b = 175; // Sandy
            break;
        case Biome::Savanna:
            r = 189; g = 183; b = 107; // Khaki
            break;
        case Biome::Jungle:
            r = 0; g = 100; b = 0; // Dark green
            break;
        case Biome::Plains:
            r = 124; g = 252; b = 0; // Lawn Green
            break;
        case Biome::Woodland:
            r = 107; g = 142; b = 35; // Olive green for distinction
            break;
        case Biome::Forest:
            r = 34; g = 139; b = 34; // Forest Green
            break;
        case Biome::Tundra:
            r = 176; g = 196; b = 222; // Light Steel Blue
            break;
        case Biome::SnowyTaiga:
            r = 255; g = 250; b = 250; // Snow
            break;
        case Biome::Mountains:
            r = 139; g = 137; b = 137; // Light Gray
            break;
        case Biome::Badlands:
            r = 210; g = 105; b = 30; // Chocolate
            break;
        case Biome::Volcano:
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