#include "debug_export.hpp"

#ifdef DEBUG_EXPORT_ENABLED

void DebugExport::writeChunkHeightmapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename)
{
    if (chunkCountX <= 0 || chunkCountZ <= 0 || !filename) return;

    const int width = chunkCountX * chunkSize;
    const int height = chunkCountZ * chunkSize;

    std::vector<uint8_t> image;
    try {
        image.resize(static_cast<size_t>(width) * height * 3);
    }
    catch (...) {
        return; // Allocation failed
    }

    int minH = INT32_MAX;
    int maxH = INT32_MIN;

    // First pass: sample heights and find min/max
    std::vector<int> heights;
    try {
        heights.resize(static_cast<size_t>(width) * height);
    }
    catch (...) {
        return;
    }

    // Generate heightmap values - every column is a pixel
    for (int cz = 0; cz < chunkCountZ; cz++) {
        for (int cx = 0; cx < chunkCountX; cx++) {
            // Get chunk column data
            auto colData = ColumnCache::getOrGenerateColumn(startChunkX + cx, startChunkZ + cz, seed);

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

void DebugExport::writeChunkBiomemapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename)
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

#else 

// If DEBUG_EXPORT_ENABLED is not defined, provide empty implementations to avoid linker errors
// This allows the code to compile in releas mode without the debug export functionality.
// The compiler will optimize these empty functions away, so they won't affect performance or binary size.
void DebugExport::writeChunkHeightmapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename) {}
void DebugExport::writeChunkBiomemapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename) {}

#endif // DEBUG_EXPORT_ENABLED