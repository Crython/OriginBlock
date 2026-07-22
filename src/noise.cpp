#include "pch.h"
#include "noise.hpp"

// Domain warping parameters
constexpr float WARP_FREQUENCY_LOW = 0.005f;     // Large-scale domain warping
constexpr float WARP_FREQUENCY_HIGH = 0.02f;     // Detail domain warping
constexpr float WARP_AMPLITUDE_LOW = 40.0f;      // Large displacement
constexpr float WARP_AMPLITUDE_HIGH = 8.0f;      // Fine displacement

// Permutation table constants for OpenSimplex2
static const int8_t perm[] = {
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
    184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
    222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180
};

static inline float grad2(int hash, float x, float y) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : 0);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

static inline int fastFloor(float x) {
    return (int)(x < 0 ? x - 1 : x);
}

uint32_t Noise::mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352D;
    x ^= x >> 15;
    x *= 0x846BA67B;
    x ^= x >> 16;
    return x;
}

uint32_t Noise::hash(int x, int z, int seed) {
    uint64_t h = static_cast<uint64_t>(x) * 0x165667B5ULL +
        static_cast<uint64_t>(z) * 0x27D4EB2BULL +
        static_cast<uint64_t>(seed) * 0x48FC803BULL;
    h = (h ^ (h >> 13)) * 0x4BF19F5DULL;
    return static_cast<uint32_t>(h ^ (h >> 16));
}

uint32_t Noise::rand_u32(uint32_t baseSeed, uint32_t index) {
    return mix(baseSeed + index * 0x9e3779b1);
}

float Noise::rand01(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (state & 0xFFFFFF) / float(0x1000000);
}

float Noise::openSimplex2(float x, float y, int seedOffset) {
    const float F2 = 0.5f * (std::sqrt(3.0f) - 1.0f);
    const float G2 = (3.0f - std::sqrt(3.0f)) / 6.0f;

    float s = (x + y) * F2;
    int i = fastFloor(x + s);
    int j = fastFloor(y + s);

    float t = (i + j) * G2;
    float X0 = i - t;
    float Y0 = j - t;
    float x0 = x - X0;
    float y0 = y - Y0;

    int i1, j1;
    if (x0 > y0) { i1 = 1; j1 = 0; }
    else { i1 = 0; j1 = 1; }

    float x1 = x0 - i1 + G2;
    float y1 = y0 - j1 + G2;
    float x2 = x0 - 1.0f + 2.0f * G2;
    float y2 = y0 - 1.0f + 2.0f * G2;

    auto hashFunc = [&](int px, int py) {
        return Noise::hash(px, py, seedOffset) & 255;
    };

    int gi0 = hashFunc(i, j);
    int gi1 = hashFunc(i + i1, j + j1);
    int gi2 = hashFunc(i + 1, j + 1);

    float t0 = 0.5f - x0 * x0 - y0 * y0;
    float n0 = t0 < 0 ? 0 : (t0 * t0 * t0 * t0 * grad2(perm[gi0], x0, y0));

    float t1 = 0.5f - x1 * x1 - y1 * y1;
    float n1 = t1 < 0 ? 0 : (t1 * t1 * t1 * t1 * grad2(perm[gi1], x1, y1));

    float t2 = 0.5f - x2 * x2 - y2 * y2;
    float n2 = t2 < 0 ? 0 : (t2 * t2 * t2 * t2 * grad2(perm[gi2], x2, y2));

    return 70.0f * (n0 + n1 + n2);
}

float Noise::ridge(float n) {
    n = std::abs(n);
    n = 1.0f - n;
    n = n * n;
    return n;
}

float Noise::heightNoise2D(int wx, int wz, int seed) {
    float sum = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;

    for (int i = 0; i < 4; ++i) {
        float n = openSimplex2(wx * freq, wz * freq, seed + i * 131);
        sum += n * amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }

    float normalized = sum * 1.4f;
    normalized = (normalized + 1.0f) / 2.0f;
    normalized = std::clamp(normalized, 0.0f, 1.0f);

    return normalized;
}

float Noise::heightNoise(int wx, int wz, int seed) {
    const int SCALE = 32;

    int x0 = floorDiv(wx, SCALE);
    int z0 = floorDiv(wz, SCALE);
    int x1 = x0 + 1;
    int z1 = z0 + 1;

    float fx = float(wx - x0 * SCALE) / SCALE;
    float fz = float(wz - z0 * SCALE) / SCALE;

    float u = fx * fx * (3.0f - 2.0f * fx);
    float v = fz * fz * (3.0f - 2.0f * fz);

    auto sample = [&](int x, int z) {
        return float(rand_u32(chunkSeed({ x,0,z }, seed), 0) & 0xFFFF) / 65535.0f;
    };

    float a = sample(x0, z0);
    float b = sample(x1, z0);
    float c = sample(x0, z1);
    float d = sample(x1, z1);

    return lerp(lerp(a, b, u), lerp(c, d, u), v);
}

float Noise::ridgedNoise(int wx, int wz, int seed) {
    float n = openSimplex2(static_cast<float>(wx), static_cast<float>(wz), seed);
    n = 1.0f - std::abs(n);
    return n * n;
}

float Noise::fbmContinent(float wx, float wz, int seed, float baseScale) {
    float baseLayer = openSimplex2(wx * baseScale * 0.5f, wz * baseScale * 0.5f, seed + 99) * 0.5f + 0.5f;

    float valleys = std::abs(openSimplex2(wx * baseScale * 1.5f, wz * baseScale * 1.5f, seed + 555));
    valleys = 1.0f - std::clamp(valleys * 3.0f, 0.0f, 1.0f);

    float islands = openSimplex2(wx * 0.02f, wz * 0.02f, seed + 777) * 0.5f + 0.5f;
    islands = std::pow(islands, 4.0f);

    float ridgedSum = 0.0f;
    float ridgedAmp = 1.0f;
    float ridgedFreq = baseScale;
    float weight = 1.0f;

    for (int i = 0; i < 2; i++) {
        float signal = ridgedNoise(static_cast<int>(wx * ridgedFreq), static_cast<int>(wz * ridgedFreq), seed + i * 131);
        signal *= weight;
        weight = std::clamp(signal * 2.0f, 0.0f, 1.0f);
        ridgedSum += signal * ridgedAmp;
        ridgedAmp *= 0.5f;
        ridgedFreq *= 2.1f;
    }

    float combined = (baseLayer * 0.5f + ridgedSum * 0.5f);
    combined -= valleys * 0.4f;
    combined += islands * 0.3f;

    return combined * 1.25f;
}

float Noise::fbmClimate(float wx, float wz, int seed, float baseFreq) {
    const int octaves = 2;
    const float lacunarity = 2.3f;
    const float gain = 0.51f;

    float sum = 0.0f;
    float amp = 1.0f;
    float freq = baseFreq;

    for (int i = 0; i < octaves; ++i) {
        sum += heightNoise2D(wx * freq, wz * freq, seed + i * 131) * amp;
        amp *= gain;
        freq *= lacunarity;
    }

    float maxPossible = (1.0f - std::pow(gain, octaves)) / (1.0f - gain);
    return sum / maxPossible;
}

float Noise::fbmWarp(float wx, float wz, int seed, float baseFreq) {
    const int octaves = 2;
    const float lacunarity = 2.0f;
    const float gain = 0.7f;

    float sum = 0.0f;
    float amp = 1.0f;
    float freq = baseFreq;

    for (int i = 0; i < octaves; ++i) {
        sum += heightNoise2D(wx * freq, wz * freq, seed + i * 131) * amp;
        amp *= gain;
        freq *= lacunarity;
    }

    float maxPossible = (1.0f - std::pow(gain, octaves)) / (1.0f - gain);
    return sum / maxPossible;
}

void Noise::domainWarp(float& x, float& z, int seed) {
    float dx = openSimplex2(x * WARP_FREQUENCY_LOW, z * WARP_FREQUENCY_LOW, seed + 9001) * WARP_AMPLITUDE_LOW;
    float dz = openSimplex2(x * WARP_FREQUENCY_LOW + 5.2f, z * WARP_FREQUENCY_LOW + 1.3f, seed + 9002) * WARP_AMPLITUDE_LOW;

    dx += openSimplex2(x * WARP_FREQUENCY_HIGH, z * WARP_FREQUENCY_HIGH, seed + 9003) * WARP_AMPLITUDE_HIGH;
    dz += openSimplex2(x * WARP_FREQUENCY_HIGH + 2.7f, z * WARP_FREQUENCY_HIGH + 4.9f, seed + 9004) * WARP_AMPLITUDE_HIGH;

    x += dx;
    z += dz;
}
