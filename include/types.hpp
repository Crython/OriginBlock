#ifndef TYPES_HPP
#define TYPES_HPP

#include "glm/glm/glm.hpp"
#pragma pack(push, 1)
struct Vertex {
    // Packed data 1 (32 bits): x(5), y(5), z(5), u(5), v(5), ao(2), unused(5)
    uint32_t data1; // x(5), y(5), z(5), u(5), v(5), ao_low(4)
    // Packed data 2 (16 bits): textureLayer(16)
    uint16_t data2; // texLayer(15), ao_high(1)
}; 
#pragma pack(pop)
static_assert(sizeof(Vertex) == 6, "Vertex struct must be exactly 6 bytes");

struct Frustum {    
    glm::vec4 planes[6]; // (normal.xyz, distance)
};

struct FaceConfig {
    int axis;      // 0=X, 1=Y, 2=Z
    int u, v;      // the 2D plane axes
    int dir;       // +1 or -1
    glm::ivec3 normal;
};

constexpr FaceConfig FACES[6] = {
    {0, 1, 2, +1, {+1,  0,  0}}, // +X
    {0, 1, 2, -1, {-1,  0,  0}}, // -X
    {1, 0, 2, +1, { 0, +1,  0}}, // +Y
    {1, 0, 2, -1, { 0, -1,  0}}, // -Y
    {2, 0, 1, +1, { 0,  0, +1}}, // +Z
    {2, 0, 1, -1, { 0,  0, -1}}, // -Z
};

// Normals for each face (matching the FACE_DIRS order)
const glm::ivec3 FACE_NORMALS[6] = {
    {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
};
// Direction vectors for each face (matching the FACE_DIRS order)
const glm::ivec3 FACE_DIRS[6] = {
    {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
};
// UV coordinates (0,0 bottom-left; 1,1 top-right)
const glm::ivec2 FACE_UVS[6][4] = {
    // +X
    {{1,1}, {0,1}, {0,0}, {1,0}},
    // -X (flip U)
    {{0,1}, {1,1}, {1,0}, {0,0}},
    // +Y
    {{1,1}, {1,0}, {0,0}, {0,1}},
    // -Y (flip V)
    {{1,0}, {1,1}, {0,1}, {0,0}},
    // +Z
    { {0,1}, {0,0}, {1,0}, {1,1}},
    // -Z (flip U)
    { {1,1}, {1,0}, {0,0}, {0,1}}
};

struct UVTransform {
    bool swapUV;
    bool flipU;
    bool flipV;
};
const UVTransform FACE_UV_TRANSFORM[6] = {
    /* +X */ { false,  false, true  },
    /* -X */ { true,  false,  true  },
    /* +Y */ { false, false, true },
    /* -Y */ { true, true, false  },
    /* +Z */ { false, false, true },
    /* -Z */ { true, true,  false }
};



enum BlockType : uint8_t {
    AIR = 0,
    DIRECTION,
	GRASS,
    DIRT,
    STONE,
    SAND,
    BEDROCK,
    WOOD,
    LEAVES,
    BLOCK_TYPE_COUNT
};
// One layer index per face per block
static constexpr uint32_t BLOCK_FACE_TEXTURE[BLOCK_TYPE_COUNT][6] = {
	// Face order: +X, -X, +Y, -Y, +Z, -Z
    { 0, 0, 0, 0, 0, 0 }, // AIR (unused)       0   
    { 1, 2, 3, 4, 5, 6 }, // DIRECTION (debug)  1
    { 8, 8, 7, 9, 8, 8 }, // GRASS              2
	{ 9, 9, 9, 9, 9, 9 }, // DIRT 			    3   
    { 10, 10, 10, 10, 10, 10 }, // STONE        4
    { 11, 11, 11, 11, 11, 11 }, // SAND         5
    { 12, 12, 12, 12, 12, 12 }, // BEDROCK      6
	{ 13, 13, 14, 14, 13, 13 }, // WOOD         7
	{ 15, 15, 15, 15, 15, 15 } // LEAVES       8
};

#endif // TYPES_HPP