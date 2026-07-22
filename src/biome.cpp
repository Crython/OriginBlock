#include "biome.hpp"

// Ocean threshold — below this continent value the area is ocean.
// Must match the value used in terrain.cpp.
static constexpr float OCEAN_THRESHOLD = 0.27f;

/*
 * Determine biome from climate parameters.
 * Uses temperature, moisture, weirdness (mountains), and continent values.
 */
Biome::BiomeType Biome::computeBiomeFromClimate(float temp, float moisture, float weird, float continent) {
    if (continent < OCEAN_THRESHOLD) {
        if (temp > 0.5f) return Biome::BiomeType::WarmOcean;
        else if (temp < 0.3) return Biome::BiomeType::ArticOcean;
		else return Biome::BiomeType::Ocean;
	}
    if (weird > 4.97f) {
        if (moisture < 0.25f && temp > 0.4) return Biome::BiomeType::Badlands;
        else if (weird >= 0.99f && temp >= 0.9f) return Biome::BiomeType::Volcano;
        else return Biome::BiomeType::Mountains;
    }
    if (temp > 0.7f) {
        if (moisture < 0.4f) return Biome::BiomeType::Desert;
        else if (moisture < 0.6f) return Biome::BiomeType::Savanna;
        else return Biome::BiomeType::Jungle;
    }
    else if (temp > 0.3f && temp < 0.7f) {
        if (moisture < 0.3f) return Biome::BiomeType::Plains;
        else if (moisture < 0.55f) return Biome::BiomeType::Woodland;
        else return Biome::BiomeType::Forest;
    }
    else {  // temp <= 0.3f
        if (moisture < 0.5f) return Biome::BiomeType::Tundra;
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