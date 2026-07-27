#ifndef BIOME_HPP
#define BIOME_HPP

#include "pch.h"
#include "noise.hpp"

class Biome {
public:

    enum class BiomeType : uint8_t {
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

    // Raw climate values at a world position, used to determine biome type.
    struct ClimateSample {
        float temp;       // [0, 1]  temperature
        float moisture;   // [0, 1]  moisture
        float weird;      // [0, 1]  weirdness / mountain tendency
        float continent;  // [0, 1]  continental scale (< threshold = ocean)
    };

    /**
     * Sample climate parameters at a domain-warped world position.
     * All four values are clamped to [0, 1].
     *
     * @param x, z  World coordinates (floats, already domain-warped by the caller if desired)
     * @param seed  World seed
     */
    static ClimateSample sampleClimate(float x, float z, int seed);

    // Returns the terrain parameters for a given biome.
    static BiomeParams getParams(BiomeType b);

    // Determines the biome from climate parameters.
    // temp, moisture, weird, continent are all in [0, 1].
    static BiomeType computeBiomeFromClimate(float temp, float moisture, float weird, float continent);
    static BiomeType assignRandomBiome(int seed);

    static const char* biomeToString(BiomeType b);

};


#endif // BIOME_HPP
