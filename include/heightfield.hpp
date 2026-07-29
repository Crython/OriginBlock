#ifndef HEIGHTFIELD_HPP
#define HEIGHTFIELD_HPP

#include "FastNoiseLite/FastNoiseLite.h"
#include "constants.hpp"

class HeightField {
public:

    // ---------- noise setup ----------
    struct TerrainNoise {

        FastNoiseLite continentNoise;   // step 1
        FastNoiseLite tectonicNoise;    // step 2
        FastNoiseLite rangeMaskNoise;   // step 3 (ridged)
        FastNoiseLite regionalNoise;    // step 4 - ridged
        FastNoiseLite localNoise;       // step 4 - ridged fbm
        FastNoiseLite terrainNoise;     // step 4 - fbm opensimplex2
        FastNoiseLite detailCellular;   // step 4 - cellular
        FastNoiseLite detailFbm;        // step 4 - fbm (blended with cellular)
        FastNoiseLite microNoise;       // step 4 - value/white
        FastNoiseLite warpNoiseX;       // step 5
        FastNoiseLite warpNoiseY;       // step 5

        float seaLevel = 0.0f; // in continent noise's [-1,1] space

        explicit TerrainNoise(int seed) {
            continentNoise.SetSeed(seed);
            continentNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            continentNoise.SetFrequency(0.00003f); // 0.00002-0.00005 range

            tectonicNoise.SetSeed(seed + 1);
            tectonicNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            tectonicNoise.SetFrequency(0.00004f);

            rangeMaskNoise.SetSeed(seed + 2);
            rangeMaskNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            rangeMaskNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
            rangeMaskNoise.SetFractalOctaves(3);
            rangeMaskNoise.SetFrequency(0.00006f);

            regionalNoise.SetSeed(seed + 3);
            regionalNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            regionalNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
            regionalNoise.SetFractalOctaves(4);
            regionalNoise.SetFrequency(1.0f / 30000.0f); // ~30km wavelength

            localNoise.SetSeed(seed + 4);
            localNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            localNoise.SetFractalType(FastNoiseLite::FractalType_Ridged);
            localNoise.SetFractalOctaves(5);
            localNoise.SetFrequency(1.0f / 6000.0f); // ~6km wavelength

            terrainNoise.SetSeed(seed + 5);
            terrainNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            terrainNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
            terrainNoise.SetFractalOctaves(6);
            terrainNoise.SetFrequency(1.0f / 1000.0f); // ~1km wavelength

            detailCellular.SetSeed(seed + 6);
            detailCellular.SetNoiseType(FastNoiseLite::NoiseType_Cellular);
            detailCellular.SetFrequency(1.0f / 100.0f);

            detailFbm.SetSeed(seed + 7);
            detailFbm.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            detailFbm.SetFractalType(FastNoiseLite::FractalType_FBm);
            detailFbm.SetFractalOctaves(3);
            detailFbm.SetFrequency(1.0f / 100.0f); // ~100m wavelength

            microNoise.SetSeed(seed + 8);
            microNoise.SetNoiseType(FastNoiseLite::NoiseType_Value);
            microNoise.SetFrequency(1.0f / 10.0f); // ~10m wavelength

            warpNoiseX.SetSeed(seed + 9);
            warpNoiseX.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            warpNoiseX.SetFrequency(0.0005f);

            warpNoiseY.SetSeed(seed + 10);
            warpNoiseY.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            warpNoiseY.SetFrequency(0.0005f);
        }
    };

    struct HeightGrid {
        int width, height;
        std::vector<float> data; // row-major, values in [0,1]


        void init(int sizeW, int sizeH) {
            width = sizeW;
            height = sizeH;
            data.resize(width * height, 0);
        }

        // Helper method to validate coordinates
        void validate(int x, int y) const {
            if (x < 0 || x >= width || y < 0 || y >= height) {
                throw std::out_of_range("HeightGrid coordinates out of bounds");
            }
        }

        float& at(int x, int y) {
            validate(x, y);
            return data[y * width + x];
        }

        float at(int x, int y) const {
            validate(x, y);
            return data[y * width + x];
        }
    };

    struct ColumnData {
        float heightMap[CHUNK_SIZE][CHUNK_SIZE];
        float maxHeight;
        float& at(int x, int y) { return heightMap[y][x]; }
        float  at(int x, int y) const { return heightMap[y][x]; }
    };


    static inline bool isOcean(TerrainNoise& n, float worldX, float worldY);
    static inline float Clamp01(float v);
    static inline float Remap(float v, float oldMin, float oldMax, float newMin, float newMax);

    static float generateHeightCell(TerrainNoise& n, float worldX, float worldY);
    static void ApplyThermalErosion(HeightGrid& g, int iterations = 5, float talusAngle = 0.02f, float amount = 0.5f);
    static void ApplyHydraulicErosion(HeightGrid& g, int dropletCount, unsigned int seed);
    static HeightGrid ComputeFlowAccumulation(const HeightGrid& g);

    static ColumnData generateHeightColumn(int chunkX, int chunkZ, unsigned int seed);
};

#endif // HEIGHTFIELD_HPP