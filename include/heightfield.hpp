#ifndef HEIGHTFIELD_HPP
#define HEIGHTFIELD_HPP

#include "biome.hpp"

class HeightField {
public:
	struct ColumnData {
		uint16_t heightMap[CHUNK_SIZE][CHUNK_SIZE];
		uint16_t maxHeight;
		Biome::BiomeType biome;
	};

	static int generateHeight(int worldX, int worldZ, const Biome::BiomeParams& biome, int seed);
	static void capRidges(ColumnData& heightmap, ColumnData* west = nullptr, ColumnData* east = nullptr, ColumnData* north = nullptr, ColumnData* south = nullptr);
	static void thermalErosion(ColumnData& heightmap, ColumnData* west = nullptr, ColumnData* east = nullptr, ColumnData* north = nullptr, ColumnData* south = nullptr);

};

#endif // HEIGHTFIELD_HPP