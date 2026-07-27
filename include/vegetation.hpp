#ifndef VEGETATION_HPP
#define VEGETATION_HPP

#include "terrain.hpp"


class Vegetation {
public:
	// Trees
	static void placeTreesInChunk(int chunkX, int chunkY, int chunkZ, int chunkSize, int seed, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE], const std::shared_ptr<HeightField::ColumnData>& colData);
	static void placeTreeAt(int x, int y, int z, uint32_t& rng, _Block(*blocks)[CHUNK_SIZE][CHUNK_SIZE]);

};

#endif // VEGETATION_HPP