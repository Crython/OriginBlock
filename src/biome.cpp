#include "biome.hpp"
#include "heightfield.hpp"


// Ocean threshold — below this continent value the area is ocean.
// Must match the value used in terrain.cpp.
static constexpr float OCEAN_THRESHOLD = 0.27f;

// Frequency constants (kept local; shared semantically with voronoi.cpp).
static constexpr float CONTINENT_FREQUENCY_B = 0.0001f;
static constexpr float CLIMATE_FREQUENCY_B   = 0.005f;
static constexpr float WEIRD_FREQUENCY_B     = 0.003f;
static constexpr float CONTINENT_SCALE_B     = 1.17f;

// Clamps v to [0, 1].
static inline float bClamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/**
 * Sample climate parameters at a world position.
 * Matches the formulas used in Voronoi::initVoronoi and Voronoi::sampleBiomeCell.
 * The caller is responsible for any domain-warping it wants applied before calling.
 */
Biome::ClimateSample Biome::sampleClimate(HeightField::TerrainNoise& noise, float x, float z, int seed) {
    ClimateSample c;

    // Temperature: latitude-driven sine + fbm noise, weights sum to <=1 so
    // clamping only ever trims float rounding, never flattens real terrain
    c.temperature = 0.5f + 0.20f * std::sin(z * 0.0005f) + 0.30f * noise.temperatureNoise.GetNoise(x, z);
    c.temperature = bClamp01(c.temperature * 0.5f + 0.5f - 0.5f + 0.5f); // no-op guard, see note below
    c.temperature = bClamp01(c.temperature);

    // Humidity: independent fbm noise layer
    c.humidity = noise.humidityNoise.GetNoise(x, z) * 0.5f + 0.5f;
    c.humidity = bClamp01(c.humidity);

    // Weirdness: ridged fbm (multi-octave)
    c.weirdness = noise.weirdnessNoise.GetNoise(x, z) * 0.5f + 0.5f;
	c.weirdness = bClamp01(Helpers::smoothstep(Helpers::smoothstep(c.weirdness) - c.humidity)); // Double nested smoothstep to reduce mid-range weirdness, favoring low/high values. Use humidity to reduce weirdness in dry areas (deserts, savannas).

	// Erosion: fbm noise, used to modulate terrain roughness
    c.erosion = noise.erosionNoise.GetNoise(x, z) * 0.5f + 0.5f;
    c.erosion = bClamp01(c.erosion);


    // Peaks: ridged fbm (multi-octave)
    c.peaks = noise.peaksNoise.GetNoise(x, z) * 0.5f + 0.5f;
    // Use erosion to reduce peaks in highly eroded areas
    c.peaks = bClamp01(c.peaks * (1.0f - c.erosion * 0.7f));


    // Continentalness: tanh soft-compresses instead of hard-clamping,
    // so land/ocean gradients survive near the coasts
    float raw = noise.continentNoiseB.GetNoise(x, z);
    c.continentalness = bClamp01(std::tanh(raw * CONTINENT_SCALE_B) * 0.5f + 0.5f);

    // Blend erosion with continentalness to soften coastlines
    c.continentalness = bClamp01(Helpers::lerp(c.continentalness, c.erosion, 0.4f));

    return c;
}

/*
 * Determine biome from climate parameters.
 * Uses temperature, humidity, weirdness (mountains), and continentalness values.
 */
Biome::BiomeType Biome::computeBiomeFromClimate(float temperature, float humidity, float weirdness, float continentalness) {
    if (continentalness < OCEAN_THRESHOLD) {
        if (temperature > 0.5f) return Biome::BiomeType::WarmOcean;
        else if (temperature < 0.3) return Biome::BiomeType::ArticOcean;
		else return Biome::BiomeType::Ocean;
	}
    if (weirdness > 4.97f) {
        if (humidity < 0.25f && temperature > 0.4) return Biome::BiomeType::Badlands;
        else if (weirdness >= 0.99f && temperature >= 0.9f) return Biome::BiomeType::Volcano;
        else return Biome::BiomeType::Mountains;
    }
    if (temperature > 0.7f) {
        if (humidity < 0.4f) return Biome::BiomeType::Desert;
        else if (humidity < 0.6f) return Biome::BiomeType::Savanna;
        else return Biome::BiomeType::Jungle;
    }
    else if (temperature > 0.3f && temperature < 0.7f) {
        if (humidity < 0.3f) return Biome::BiomeType::Plains;
        else if (humidity < 0.55f) return Biome::BiomeType::Woodland;
        else return Biome::BiomeType::Forest;
    }
    else {  // temperature <= 0.3f
        if (humidity < 0.5f) return Biome::BiomeType::Tundra;
        else return Biome::BiomeType::SnowyTaiga;
    }
	return Biome::BiomeType::None;  // Fallback, shows when something goes wrong
}

/**
 * Get biome parameters (base height, amplitude, mountain strength).
 * These control terrain generation differently for each biome.
 */
Biome::BiomeParams Biome::getParams(Biome::BiomeType b)
{
	// Parameters: baseHeight, amplitude, mountainStrength, treeDensity, treeMaxBaseHeight
    switch (b) {
    case Biome::BiomeType::Ocean:
        return { 8.0f, 4.0f, 0.0f, 0.0f, 0.0f };

    case Biome::BiomeType::WarmOcean:
        return { 12.0f, 5.0f, 0.0f, 0.0f, 0.0f };

    case Biome::BiomeType::ArticOcean:
        return { 6.0f, 3.0f, 0.0f, 0.0f, 0.0f };

    case Biome::BiomeType::Desert:
        return { 62.0f, 4.0f, 0.2f, 0.0f, 0.0f };

    case Biome::BiomeType::Savanna:
        return { 68.0f, 6.0f, 0.1f, 0.6f, 110.0f };

    case Biome::BiomeType::Jungle:
        return { 70.0f, 8.0f, 0.2f, 6.0f, 125.0f };

    case Biome::BiomeType::Plains:
        return { 64.0f, 6.0f, 0.0f, 1.2f, 115.0f };

    case Biome::BiomeType::Woodland:
        return { 65.0f, 6.5f, 0.1f, 2.8f, 120.0f };

    case Biome::BiomeType::Forest:
        return { 66.0f, 7.0f, 0.2f, 4.5f, 120.0f };

    case Biome::BiomeType::Tundra:
        return { 58.0f, 5.0f, 0.0f, 0.3f, 90.0f };

    case Biome::BiomeType::SnowyTaiga:
        return { 54.0f, 4.0f, 0.1f, 2.0f, 95.0f };

    case Biome::BiomeType::Mountains:
        return { 80.0f, 280.0f, 1.0f, 0.25f, 95.0f };

    case Biome::BiomeType::Badlands:
        return { 72.0f, 175.0f, 1.0f, 0.05f, 90.0f };

    case Biome::BiomeType::Volcano:
        return { 90.0f, 280.0f, 1.0f, 0.0f, 0.0f };
    }

    return { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
}

Biome::BiomeType Biome::assignRandomBiome(int seed) {
    int rnd = static_cast<int>(Noise::heightNoise2D(static_cast<float>(seed), 0.0f, seed) * 14);  // Limit to 14 for defined cases
    switch (rnd % 14) {
    case 0: return Biome::BiomeType::WarmOcean;  // Optional: Bias some to ocean if needed
    case 1: return Biome::BiomeType::ArticOcean;
    case 2: return Biome::BiomeType::Desert;
    case 3: return Biome::BiomeType::Savanna;
    case 4: return Biome::BiomeType::Jungle;
    case 5: return Biome::BiomeType::Plains;
    case 6: return Biome::BiomeType::Woodland;
    case 7: return Biome::BiomeType::Forest;
    case 8: return Biome::BiomeType::Tundra;
    case 9: return Biome::BiomeType::SnowyTaiga;
    case 10: return Biome::BiomeType::Mountains;
    case 11: return Biome::BiomeType::Badlands;
    case 12: return Biome::BiomeType::Volcano;
    case 13: return Biome::BiomeType::Ocean;
    // Fallback to a valid biome
    default: return Biome::BiomeType::Plains;  // Safety net, though %14 should prevent this
    }
}

const char* Biome::biomeToString(Biome::BiomeType b) {
    switch (b) {
    case Biome::BiomeType::None: return "None";
    case Biome::BiomeType::Ocean: return "Ocean";
    case Biome::BiomeType::WarmOcean: return "Warm Ocean";
    case Biome::BiomeType::ArticOcean: return "Arctic Ocean";
    case Biome::BiomeType::Desert: return "Desert";
    case Biome::BiomeType::Savanna: return "Savanna";
    case Biome::BiomeType::Jungle: return "Jungle";
    case Biome::BiomeType::Plains: return "Plains";
    case Biome::BiomeType::Woodland: return "Woodland";
    case Biome::BiomeType::Forest: return "Forest";
    case Biome::BiomeType::Tundra: return "Tundra";
    case Biome::BiomeType::SnowyTaiga: return "Snowy Taiga";
    case Biome::BiomeType::Mountains: return "Mountains";
    case Biome::BiomeType::Badlands: return "Badlands";
    case Biome::BiomeType::Volcano: return "Volcano";
    default: return "Unknown";
    }
}