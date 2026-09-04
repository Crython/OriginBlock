#include "debug_export.hpp"

#ifdef DEBUG_EXPORT_ENABLED

#include "biome.hpp"
#include "column_cache.hpp"


void DebugExport::writeChunkHeightmapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename)
{
    if (chunkCountX <= 0 || chunkCountZ <= 0 || !filename) return;
    std::cerr << "Starting heightmap generation" << std::endl;

    const int width = chunkCountX * chunkSize;
    const int height = chunkCountZ * chunkSize;

    std::vector<uint8_t> image;
    try {
        image.resize(static_cast<size_t>(width) * height * 3);
    }
    catch (...) {
        std::cerr << "Failed to allocate memory for heightmap image of size " << width << "x" << height << std::endl;  
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
        std::cerr << "Failed to allocate memory for heightmap data of size " << width << "x" << height << std::endl;
        return;
    }

    // Generate heightmap values - every column is a pixel
    for (int cz = 0; cz < chunkCountZ; cz++) {
        for (int cx = 0; cx < chunkCountX; cx++) {
            // Get chunk column data
            auto colData = ColumnCache::getOrGenerateColumn(startChunkX + cx, startChunkZ + cz, seed);

            for (int lz = 0; lz < chunkSize; lz++) {
                for (int lx = 0; lx < chunkSize; lx++) {
                    int h = colData->heightMap[lz][lx];

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

    std::cout << "Heightmap exported to " << filename << " (minH: " << minH << ", maxH: " << maxH << ")" << std::endl;
}

namespace fs = std::filesystem;

void DebugExport::writeClimateParametersPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int seed)
{
    if (chunkCountX <= 0 || chunkCountZ <= 0) return;

    const int width = chunkCountX;
    const int height = chunkCountZ;
    const size_t pixelCount = static_cast<size_t>(width) * height;

    // 1. Create the nested folders: BiomeParameters/<seed>
    fs::path exportDir = fs::path("BiomeParameters") / std::to_string(seed);
    try {
        fs::create_directories(exportDir);
    }
    catch (const fs::filesystem_error& e) {
        std::cerr << "Failed to create directories: " << e.what() << '\n';
        return;
    }

    // 2. Pre-allocate flat arrays for all 6 grayscale images (1 byte per pixel)
    std::vector<uint8_t> continentalness;
    std::vector<uint8_t> peaks;
    std::vector<uint8_t> temperature;
    std::vector<uint8_t> humidity;
    std::vector<uint8_t> weirdness;
    std::vector<uint8_t> erosion;

    try {
        continentalness.resize(pixelCount);
        peaks.resize(pixelCount);
        temperature.resize(pixelCount);
        humidity.resize(pixelCount);
        weirdness.resize(pixelCount);
        erosion.resize(pixelCount);
    }
    catch (...) {
        std::cerr << "Memory allocation failed for climate maps.\n";
        return;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    // 3. Single pass: Sample climate and write to pixel buffers simultaneously
    HeightField::TerrainNoise tn(seed);

    for (int cz = 0; cz < height; ++cz) {
        for (int cx = 0; cx < width; ++cx) {
            float bx = static_cast<float>((startChunkX + cx) * CHUNK_SIZE);
            float bz = static_cast<float>((startChunkZ + cz) * CHUNK_SIZE);

            Biome::ClimateSample sample = Biome::sampleClimate(tn, bx, bz, seed);

            // Calculate 1D index for the flat vectors
            size_t i = static_cast<size_t>(cz) * width + cx;

            // Convert normalized float [0.0, 1.0] to uint8_t [0, 255]. 
            // std::clamp ensures out-of-bounds floats don't wrap around and cause visual glitches.
            continentalness[i] = static_cast<uint8_t>(std::clamp(sample.continentalness, 0.0f, 1.0f) * 255.0f);
            peaks[i] = static_cast<uint8_t>(std::clamp(sample.peaks, 0.0f, 1.0f) * 255.0f);
            temperature[i] = static_cast<uint8_t>(std::clamp(sample.temperature, 0.0f, 1.0f) * 255.0f);
            humidity[i] = static_cast<uint8_t>(std::clamp(sample.humidity, 0.0f, 1.0f) * 255.0f);
            weirdness[i] = static_cast<uint8_t>(std::clamp(sample.weirdness, 0.0f, 1.0f) * 255.0f);
            erosion[i] = static_cast<uint8_t>(std::clamp(sample.erosion, 0.0f, 1.0f) * 255.0f);
        }
    }

    auto sampleEndTime = std::chrono::high_resolution_clock::now();
    std::cout << "Climate sampling took: "
        << std::chrono::duration<double>(sampleEndTime - startTime).count()
        << " seconds.\n";

    // 4. Helper lambda to write a single PNG to the correct folder
    auto saveMap = [&](const std::string& name, const std::vector<uint8_t>& data) {
        fs::path filePath = exportDir / (name + ".png");
        // Channels set to 1 for grayscale. Stride is just 'width' since it's 1 byte per pixel.
        stbi_write_png(filePath.string().c_str(), width, height, 1, data.data(), width);
        };

    // 5. Save all 6 images
    saveMap("continentalness", continentalness);
    saveMap("peaks", peaks);
    saveMap("temperature", temperature);
    saveMap("humidity", humidity);
    saveMap("weirdness", weirdness);
    saveMap("erosion", erosion);

    auto totalEndTime = std::chrono::high_resolution_clock::now();
    std::cout << "Total export (including PNG compression) took: "
        << std::chrono::duration<double>(totalEndTime - startTime).count()
        << " seconds.\n";
}

#else 

// If DEBUG_EXPORT_ENABLED is not defined, provide empty implementations to avoid linker errors
// This allows the code to compile in releas mode without the debug export functionality.
// The compiler will optimize these empty functions away, so they won't affect performance or binary size.
void DebugExport::writeChunkHeightmapPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int chunkSize, int seed, const char* filename) { std::cout << "Heightmap: Debug export not enabled.\n";}
void DebugExport::writeClimateParametersPNG(int startChunkX, int startChunkZ, int chunkCountX, int chunkCountZ, int seed) { std::cout << "Climate Parameters: Debug export not enabled.\n";}

#endif // DEBUG_EXPORT_ENABLED