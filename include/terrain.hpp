#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include "chunkHandler.hpp"
#include "voronoi.hpp"
#include "heightfield.hpp"
#include "column_cache.hpp"
#include "vegetation.hpp"



// static class Terrain - responsible for generating terrain chunks based on heightmaps and biome data
// A wrapper class is needed to have multiple worlds generated at the same time, as the Voronoi and ColumnCache classes are static and would otherwise share state between worlds.
class Terrain {
private:
	// Total number of chunks generated (for debugging)
	static int totalChunksGenerated;

public:

	// Clear the static member variables (for debugging) (not the states of generation elements)
	static void clearCounters() { totalChunksGenerated = 0; }
	static int getTotalChunksGenerated() { return totalChunksGenerated; }


    // Use pointer to 3D array of _Block with CHUNK_SIZE in each dimension
    static void generate(const ChunkCoord& chunkPos, const int seed, _Block (*blocks)[CHUNK_SIZE][CHUNK_SIZE]);

    // Generate a pseudo random unsigned integer
    static uint32_t setRandSeed(void* instancePtr);
    

    // Wrapper functions
	static void initVoronoi(int seed, int numSites, float mapSize, float startX, float startZ) {
		Voronoi::initVoronoi(seed, numSites, mapSize, startX, startZ);
	}
    static std::shared_ptr<HeightField::ColumnData> getOrGenerateColumn(int x, int z, int seed) {
		return ColumnCache::getOrGenerateColumn(x, z, seed);
    }
    
};

#endif // TERRAIN_HPP