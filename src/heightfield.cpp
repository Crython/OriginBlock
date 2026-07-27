#include "heightfield.hpp"
#include "helpers.hpp"

// Terrain erosion parameters
constexpr int EROSION_PASSES = 2;
constexpr int TALUS = 3;        // max allowed height difference

// Limits
constexpr float MAX_TERRAIN_HEIGHT = 1024.0f;        // Maximum terrain height in blocks
constexpr float BASE_TERRAIN_HEIGHT = 48.0f;        // Base terrain height in blocks
constexpr float MIN_TERRAIN_HEIGHT = 32.0f;         // Minimum terrain height in blocks
constexpr float INV_MAX_MOUNTAIN_ADD = 1.0f / MAX_TERRAIN_HEIGHT;


/*
 * Generate terrain height at a world coordinate.
 * Combines biome parameters with multi-octave noise for varied terrain.
 */
int HeightField::generateHeight(int worldX, int worldZ, const Biome::BiomeParams& biome, int seed)
{
    // 1. Cast integers to floats exactly once at the top.
    // Implicit conversions inside function calls can sometimes add overhead.
    const float fx = static_cast<float>(worldX);
    const float fz = static_cast<float>(worldZ);

    // 2. High peaks, but changes slowly
    float continentNoise = Helpers::remap01(Noise::openSimplex2(fx * 0.0008f, fz * 0.0008f, seed + 11));
    float continent = Helpers::smoothstep(continentNoise);
    continent *= continent;

    // 3. Main terrain shape
    float baseNoise = Helpers::remap01(Noise::openSimplex2(fx * 0.004f, fz * 0.004f, seed + 23));
    float height = biome.baseHeight + baseNoise * biome.amplitude * 0.35f;
    height *= continent;

    // 4. Do extra continent shaping for mountains
    if (biome.mountainStrength > 0.0f) {
        float mountainMask = Helpers::remap01(Noise::openSimplex2(fx * 0.0015f, fz * 0.0015f, seed + 47));

        // Fold the multiplication into the smoothstep output immediately
        mountainMask = Helpers::smoothstep(mountainMask) * biome.mountainStrength;

        // Height-relative taper using multiplication instead of division
        float relative = Helpers::clamp01((height - biome.baseHeight) * INV_MAX_MOUNTAIN_ADD);

        // Aggressive taper
        float taper = 1.0f - (relative * relative * relative);

        height += mountainMask * MAX_TERRAIN_HEIGHT * taper;
    }

    // 5. Cache the shared frequency coordinate! 
    // Both 'detail' and 'ridgedNoise' use fx * 0.02f. Calculate it once.
    const float detailX = fx * 0.02f;

    // Fine detail
    height += Noise::openSimplex2(detailX, fz * 0.02f, seed + 91) * 2.5f;
    height += BASE_TERRAIN_HEIGHT;

    // Slight ridged noise for sharpness
    height += Noise::ridgedNoise(detailX, fz * 0.019f, seed);

    // Clamp to valid and reasonable range
    return static_cast<int>(std::clamp(height, (float)MIN_TERRAIN_HEIGHT, (float)MAX_TERRAIN_HEIGHT));
}


void HeightField::capRidges(ColumnData& heightmap, ColumnData* west, ColumnData* east, ColumnData* north, ColumnData* south)
{
    const float RIDGE_CAP = 8.0f;
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            float h = (float)heightmap.heightMap[x][z];

            // Check 4 directions for hard ridge capping
            struct { int dx, dz; ColumnData* col; } neighbors[4] = {
                { 1, 0,  (x < CHUNK_SIZE - 1) ? &heightmap : east },
                {-1, 0,  (x > 0) ? &heightmap : west },
                { 0, 1,  (z < CHUNK_SIZE - 1) ? &heightmap : south },
                { 0,-1,  (z > 0) ? &heightmap : north }
            };

            for (int i = 0; i < 4; i++) {
                if (!neighbors[i].col) continue;

                int nx = (x + neighbors[i].dx + CHUNK_SIZE) % CHUNK_SIZE;
                int nz = (z + neighbors[i].dz + CHUNK_SIZE) % CHUNK_SIZE;

                float nh = (float)neighbors[i].col->heightMap[nx][nz];
                h = nh + Helpers::slopeLimit(h - nh, RIDGE_CAP);
            }
            heightmap.heightMap[x][z] = (uint16_t)h;
        }
    }
}

void HeightField::thermalErosion(ColumnData& heightmap, ColumnData* west, ColumnData* east, ColumnData* north, ColumnData* south)
{
    for (int pass = 0; pass < EROSION_PASSES; pass++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                int current = heightmap.heightMap[x][z];

                // Check 4 directions: East, West, South, North
                struct { int dx, dz; ColumnData* col; bool isInternal; } neighbors[4] = {
                    { 1, 0,  (x < CHUNK_SIZE - 1) ? &heightmap : east,  (x < CHUNK_SIZE - 1) },
                    {-1, 0,  (x > 0) ? &heightmap : west,               (x > 0) },
                    { 0, 1,  (z < CHUNK_SIZE - 1) ? &heightmap : south, (z < CHUNK_SIZE - 1) },
                    { 0,-1,  (z > 0) ? &heightmap : north,              (z > 0) }
                };

                for (int i = 0; i < 4; i++) {
                    if (!neighbors[i].col) continue;

                    int nx = (x + neighbors[i].dx + CHUNK_SIZE) % CHUNK_SIZE;
                    int nz = (z + neighbors[i].dz + CHUNK_SIZE) % CHUNK_SIZE;

                    uint16_t* nPtr = &neighbors[i].col->heightMap[nx][nz];
                    int diff = current - *nPtr;

                    if (diff > TALUS) {
                        int move = (diff - TALUS) / 2;
                        heightmap.heightMap[x][z] -= move;
                        current -= move; // Update current to reflect change for next neighbor

                        // Only mutate if the neighbor is within the same column
                        if (neighbors[i].isInternal) {
                            *nPtr += move;
                        }
                    }
                }
            }
        }
    }
}
