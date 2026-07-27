#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include "chunkHandler.hpp"
#include "stb/stb_image_write.h"
#include "noise.hpp"
#include "biome.hpp"
#include "voronoi.hpp"
#include "heightfield.hpp"



// Fix: Use correct 3D array type for blocks parameter
class Terrain {
public:
	Biome biome;

   

    // Use pointer to 3D array of _Block with CHUNK_SIZE in each dimension
    static void generate(const ChunkCoord& chunkPos, const int seed, _Block (*blocks)[CHUNK_SIZE][CHUNK_SIZE]);
    static void clearCache();

    // Generate a pseudo random unsigned integer
    static uint32_t setRandSeed(void* instancePtr);
    
    static void writeChunkHeightmapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename);
    static void writeChunkBiomemapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename);
	static void initVoronoi(int seed, int numSites, float mapSize, float startX, float startZ) {
		Voronoi::initVoronoi(seed, numSites, mapSize, startX, startZ);
	}


    static std::shared_ptr<HeightField::ColumnData> getOrGenerateColumn(int x, int z, int seed);

private:


    

    static Biome::BiomeType assignRandomBiome(int seed);
    static uint32_t chunkSeed(const ChunkCoord& c, int seed);

    static float terrace(float h, float step, float strength);
    static void placeTreesInChunk(int chunkX, int chunkY, int chunkZ, int chunkSize, int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE], const std::shared_ptr<HeightField::ColumnData>& colData);
    static void placeTreeAt(int x, int y, int z, uint32_t& rng, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE]);

    static std::unordered_map<uint64_t, std::shared_ptr<HeightField::ColumnData>> columnCache; 
    static std::mutex cacheMutex;

    static uint64_t packCoords(int x, int z) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
    }
    

};




#endif // TERRAIN_HPP