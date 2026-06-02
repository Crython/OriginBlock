
#ifndef CONSTANTS_HPP
#define CONSTANTS_HPP

#include <utility>

// Window constants
constexpr size_t WINDOW_WIDTH = 1920; // 1280
constexpr size_t WINDOW_HEIGHT = 1080; // 720

// Math constants
constexpr float PI = 3.14159265358979323846f;


// World constants
constexpr int CHUNK_SIZE = 16; // Size of a chunk in blocks (16x16x16) DO NOT CHANGE!!!!
constexpr size_t MAX_CHUNK_UPDATES_PER_FRAME = 512;
constexpr size_t MAX_CHUNKS_LOADED_PER_FRAME = 512;
constexpr size_t MAX_CHUNKS_POPPED_PER_FRAME = 3072;


constexpr int RENDER_RADIUS = 40;        // -X/X, -Z/Z
constexpr int MAX_NEG_RENDER_RADIUS_Y = 15; // -Y (chunk should be <2 pixels total for 32)
constexpr int POS_RENDER_RADIUS_Y = 10;  // +Y

// Chunk constants
constexpr int PAD = 1;
constexpr int PADDED_CHUNK_SIZE = CHUNK_SIZE + 2 * PAD;

// Each chunk at a set distance from the player uses its closest corresponding LOD from the array
// Distances are pre-squared: 0^2, 15^2, 30^2, 45^2, 60^2
constexpr std::pair<int, int> CHUNK_LOD_LEVEL_DISTANCES[5] = {
    {0, 1},
    {225, 2},    // 15 * 15
    {900, 4},    // 30 * 30
    {2025, 8},   // 45 * 45
    {3600, 16}   // 60 * 60
}; 
constexpr int MAX_LOD_RADIUS = 4; // Chunks in this range have to have the highest LOD
constexpr int MAX_LOD_RADIUS_SQUARED = MAX_LOD_RADIUS * MAX_LOD_RADIUS; // Precalculate the constant

// Physics constants
constexpr float GRAVITY = -24.0f;

/*
* Jump height formula (in blocks) for GRAVITY as -24.0:
* height = 0.0225(JUMP_FORCE)^2 - 0.0275(JUMP_FORCE) + 0.058125
* 
* force for 1   block  11 + 2sqrt(3421) / 18 = 7.10992
* fore for 1.25 blocks 11 + sqrt(4321)  / 18 = 7.91492
* force for 1.5 blocks 15 + sqrt(5221)  / 18 = 8.63961
* force for 2   blocks 18 + 2sqrt(7021) / 18 = 9.92127
*/
constexpr float JUMP_FORCE = 7.91492;
constexpr float PLAYER_WIDTH = 0.6f;
constexpr float PLAYER_HEIGHT = 1.8f;
constexpr float PLAYER_EYE_HEIGHT = 1.6f;

#endif // CONSTANTS_HPP
