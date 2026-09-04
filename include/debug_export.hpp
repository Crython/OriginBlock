#ifndef DEBUG_EXPORT_HPP
#define DEBUG_EXPORT_HPP

#include "stb/stb_image_write.h" // For writing PNG files
#include <iostream>

class DebugExport {
public:

    static void writeChunkHeightmapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename);
    static void writeClimateParametersPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int seed);

};

#endif // DEBUG_EXPORT_HPP