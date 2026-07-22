#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include "chunkHandler.hpp"
#include "stb/stb_image_write.h"

enum class Biome : uint8_t {
	None = 0,
    Ocean,
	WarmOcean,
	ArticOcean,
    Desert,
    Savanna,
    Jungle,
    Plains,
    Woodland,
	Forest,
    Tundra, 
    SnowyTaiga,
    Mountains,
    Badlands,
    Volcano
};
struct BiomeParams {
    float baseHeight;
    float amplitude;
    float mountainStrength;

    float treeDensity;   // trees per chunk (avg)
    float treeLine;      // max height where trees grow
};

struct VoronoiSite {
    float x, z;
    Biome biome;  // Assigned biome for the site
};

struct VoronoiSpatialGrid {
    std::vector<std::vector<size_t>> grid; // Indices into voronoiSites
    float startX, startZ;
    float mapSize;
    float cellSize;
    int cols, rows;

    void clear() {
        grid.clear();
        cols = rows = 0;
    }
};

// Fix: Use correct 3D array type for blocks parameter
class Terrain {
public:
    struct ColumnData {
        uint16_t heightMap[CHUNK_SIZE][CHUNK_SIZE];
        uint16_t maxHeight;
        Biome biome;
    };

    // Use pointer to 3D array of _Block with CHUNK_SIZE in each dimension
    static void generate(const ChunkCoord& chunkPos, const int seed, _Block (*blocks)[CHUNK_SIZE][CHUNK_SIZE]);
    static void clearCache();

    // Generate a pseudo random unsigned integer
    static uint32_t setRandSeed(void* instancePtr);
    
    static void writeChunkHeightmapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename);
    static void writeChunkBiomemapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename);

    static void initVoronoi(int seed, int numSites, float mapSize, float startX, float startZ);

    static std::shared_ptr<ColumnData> getOrGenerateColumn(int x, int z, int seed);

private:
    static std::vector<VoronoiSite> voronoiSites;
    static VoronoiSpatialGrid voronoiGrid;

    

	static float openSimplex2(float x, float y, int seedOffset = 0); // 2D OpenSimplex2 noise
    static void domainWarp(float& x, float& z, int seed);
    static Biome assignRandomBiome(int seed);
    static uint32_t mix(uint32_t x);
    static uint32_t chunkSeed(const ChunkCoord& c, int seed);
    static uint32_t rand_u32(uint32_t baseSeed, uint32_t index);
    static int generateHeight(int worldX, int worldZ, const BiomeParams& biome, int seed);
    static void capRidges(ColumnData& heightmap, ColumnData* west = nullptr, ColumnData* east = nullptr, ColumnData* north = nullptr, ColumnData* south = nullptr);
    static void thermalErosion(ColumnData& heightmap, ColumnData* west = nullptr, ColumnData* east = nullptr, ColumnData* north = nullptr, ColumnData* south = nullptr);
    static uint32_t hash(int x, int z, int seed);
    static Biome sampleBiomeCell(int bx, int bz, int seed);
    static BiomeParams sampleBlendedBiomeParams(int worldX, int worldZ, int seed);
    static BiomeParams getParams(Biome b);
    static inline int floorDiv(int a, int b);
    static float heightNoise(int worldX, int worldZ, int seed);
    static float heightNoise2D(int worldX, int worldZ, int seed);
    static float ridge(float n);
    static float terrace(float h, float step, float strength);
	static float fbmContinent(float wx, float wz, int seed, float baseScale); // fractal Brownian motion
    static float fbmClimate(float wx, float wz, int seed, float baseFreq);
    static float fbmWarp(float wx, float wz, int seed, float baseFreq);
    static float ridgedNoise(int wx, int wz, int seed);
    static void placeTreesInChunk(int chunkX, int chunkY, int chunkZ, int chunkSize, int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE], const std::shared_ptr<ColumnData>& colData);
    static void placeTreeAt(int x, int y, int z, uint32_t& rng, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE]);

    static std::unordered_map<uint64_t, std::shared_ptr<ColumnData>> columnCache; 
    static std::mutex cacheMutex;

    static uint64_t packCoords(int x, int z) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
    }
    

};




#endif // TERRAIN_HPP