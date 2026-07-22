#ifndef NOISE_HPP
#define NOISE_HPP

#include "pch.h"
#include "chunkHandler.hpp"


class Noise {
public:
    static float openSimplex2(float x, float y, int seedOffset = 0);
    static uint32_t hash(int x, int z, int seed);
    static uint32_t mix(uint32_t x);
    static uint32_t rand_u32(uint32_t baseSeed, uint32_t index);
    static float rand01(uint32_t& state);
    static float heightNoise(int worldX, int worldZ, int seed);
    static float heightNoise2D(int worldX, int worldZ, int seed);
    static float ridge(float n);
    static float ridgedNoise(int wx, int wz, int seed);
    static float fbmContinent(float wx, float wz, int seed, float baseScale);
    static float fbmClimate(float wx, float wz, int seed, float baseFreq);
    static float fbmWarp(float wx, float wz, int seed, float baseFreq);
    static void domainWarp(float& x, float& z, int seed);

    static inline int floorDiv(int a, int b) {
        return (a >= 0) ? (a / b) : ((a - b + 1) / b);
    }

private:
    static inline float lerp(float a, float b, float t) {
        return a + (b - a) * t;
    }

    static inline uint32_t chunkSeed(const ChunkCoord& c, int seed) {
        uint32_t h = 0;
        h ^= mix(static_cast<uint32_t>(c.x));
        h ^= mix(static_cast<uint32_t>(c.y) + 0x9e3779b1);
        h ^= mix(static_cast<uint32_t>(c.z) + 0x85F99D69);
        h ^= mix(static_cast<uint32_t>(seed));
        return mix(h);
    }
};

#endif // NOISE_HPP
