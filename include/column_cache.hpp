#ifndef COLUMN_CACHE_HPP
#define COLUMN_CACHE_HPP

#include "voronoi.hpp"
#include "heightfield.hpp"

class ColumnCache {
public:


	static void clearCache();
	// Get column from cache or generate it with the same generator
	static std::shared_ptr<HeightField::ColumnData> getOrGenerateColumn(int x, int z, int seed);

	static uint64_t packCoords(int x, int z) {
		return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
	}

private:
	static std::unordered_map<uint64_t, std::shared_ptr<HeightField::ColumnData>> columnCache;
	static std::mutex cacheMutex;

};

#endif // COLUMN_CACHE_HPP