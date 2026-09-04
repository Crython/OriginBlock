#ifndef CLIMATE_TYPES_HPP
#define CLIMATE_TYPES_HPP

// Lightweight header: shared climate data used by both Biome and HeightField.
// Extracted to break the biome.hpp <-> heightfield.hpp circular dependency.

struct ClimateSample {
    float continentalness;  // [0, 1]  continental scale
	float erosion;          // [0, 1]  erosion / roughness
	float peaks;            // [0, 1]  peaks / mountain height
    float temperature;      // [0, 1]  temperature
	float humidity;         // [0, 1]  humidity / moisture
    float weirdness;        // [0, 1]  weirdness / mountain tendency
};

#endif // CLIMATE_TYPES_HPP
