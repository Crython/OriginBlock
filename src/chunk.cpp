/*
 * CHUNK.CPP
 * * Manages individual chunk operations including:
 * - Terrain generation and block storage
 * - Greedy mesh generation for efficient rendering
 * - Neighbor padding system for seamless chunk boundaries
 * - Dirty state tracking for incremental updates
 * * The greedy meshing algorithm combines adjacent faces with the same texture/orientation
 * into larger quads to minimize draw calls. Neighbor padding allows chunks to mesh
 * independently by storing adjacent chunk boundary information.
 */
#include "pch.h"

#include "chunkHandler.hpp"
#include "Worlds.hpp"
#include "terrain.hpp"


 // ===========================
 // MESH GENERATION CONSTANTS
 // ===========================

 // Initial memory reservation sizes for mesh data
constexpr size_t MESH_VERTEX_RESERVE = 8192;   // Typical vertices per chunk
constexpr size_t MESH_INDEX_RESERVE = 12288;   // Typical indices per chunk (2 triangles per quad)

// Vertex packing bit layout constants (data1: 32-bit)
constexpr uint32_t VERTEX_X_BITS = 5;
constexpr uint32_t VERTEX_Y_BITS = 5;
constexpr uint32_t VERTEX_Z_BITS = 5;
constexpr uint32_t VERTEX_U_BITS = 5;
constexpr uint32_t VERTEX_V_BITS = 5;
constexpr uint32_t VERTEX_NORMAL_BITS = 3;
constexpr uint32_t VERTEX_AO_LOW_BITS = 4;

// Vertex packing bit offsets
constexpr uint32_t VERTEX_X_SHIFT = 0;
constexpr uint32_t VERTEX_Y_SHIFT = 5;
constexpr uint32_t VERTEX_Z_SHIFT = 10;
constexpr uint32_t VERTEX_U_SHIFT = 15;
constexpr uint32_t VERTEX_V_SHIFT = 20;
constexpr uint32_t VERTEX_NORMAL_SHIFT = 25;
constexpr uint32_t VERTEX_AO_LOW_SHIFT = 28;

// Vertex packing masks
constexpr uint32_t VERTEX_5BIT_MASK = 0x1Fu;
constexpr uint32_t VERTEX_3BIT_MASK = 0x7u;
constexpr uint32_t VERTEX_4BIT_MASK = 0xFu;

// Default light level for blocks with no explicit lighting
constexpr uint8_t DEFAULT_LIGHT_LEVEL = 5;


// ===========================
// CONSTRUCTORS & INITIALIZATION
// ===========================

// Original constructor - generates immediately
Chunk::Chunk(const ChunkCoord& coord, World* world, int seed) : owner(world)
{
    setPosition(coord);
    generate(seed);
}

// Constructor for threaded generation - optionally defer generation
Chunk::Chunk(const ChunkCoord& coord, World* world, int seed, bool doGenerate) : owner(world)
{
    setPosition(coord);
    if (doGenerate) {
        generate(seed);
    }
}

Chunk::~Chunk()
{
    meshCPU.clear();
    meshGPU.destroy();
}

bool Chunk::isAir(const glm::ivec3 local) {
    return blocks[local.x][local.y][local.z].isAir();
}

// ===========================
// BLOCK MANIPULATION
// ===========================

void Chunk::setPosition(ChunkCoord c)
{
    chunkX = c.x;
    chunkY = c.y;
    chunkZ = c.z;
}

/**
 * Set a block at a local position within this chunk.
 * Updates the solid block count and validates bounds.
 */
void Chunk::setBlock(const glm::ivec3& localPos, BlockType type, uint8_t metadata) {
    if (localPos.x < 0 || localPos.y < 0 || localPos.z < 0 || localPos.x >= CHUNK_SIZE || localPos.y >= CHUNK_SIZE || localPos.z >= CHUNK_SIZE) return;

    _Block& b = blocks[localPos.x][localPos.y][localPos.z];
    bool wasAir = b.isAir();
    b.setValues(type, metadata, 0);
    bool isAir = b.isAir();

    if (wasAir && !isAir) solidBlockCount++;
    else if (!wasAir && isAir) solidBlockCount--;

    rebuildBlockMipmaps();
}

// ===========================
// MESH MANAGEMENT
// ===========================

void Chunk::clearMesh() {
    meshCPU.clear();
}

// ===========================
// DIRTY STATE MANAGEMENT
// ===========================

uint8_t Chunk::getDirtyFlags() const {
    return dirtyFlags;
}

bool Chunk::isDirty(DirtyFlags df) const {
    return (dirtyFlags & df) != 0;
}

void Chunk::markDirty(DirtyFlags df) {
    dirtyFlags |= df;
    if (owner) owner->markDirty(this);
}

void Chunk::clearDirty(DirtyFlags df) {
    dirtyFlags &= ~df; // dirtyFlags = dirtyFlags AND (NOT df)
}

// ===========================
// TERRAIN GENERATION
// ===========================

void Chunk::generate(int seed)
{
    Terrain::generate({ chunkX, chunkY, chunkZ }, seed, blocks);
    countSolidBlocks();
    rebuildBlockMipmaps();
}

void Chunk::generateTerrain(int seed)
{
    Terrain::generate({ chunkX, chunkY, chunkZ }, seed, blocks);
    countSolidBlocks();
    rebuildBlockMipmaps();
}

/**
 * Count all non-air blocks in this chunk.
 * Used to detect empty chunks and for optimization decisions.
 */
void Chunk::countSolidBlocks()
{
    solidBlockCount = 0;
    for (int x = 0; x < CHUNK_SIZE; ++x) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int z = 0; z < CHUNK_SIZE; ++z) {
                if (!blocks[x][y][z].isAir()) {
                    solidBlockCount++;
                }
            }
        }
    }
}

bool Chunk::hasMesh() const {
    return meshGPU.indexCount > 0;
}


void Chunk::uploadMesh()
{
    meshGPU.upload(meshCPU);

    // Only clear if not marked dirty
    if (!isDirty(Dirty_Mesh)) {
        meshCPU.clear();

        for (int i = 0; i < 6; ++i) {
            meshCPU.vertices[i].shrink_to_fit();
            meshCPU.indices[i].shrink_to_fit();
        }
    }
}

void Chunk::draw(int& totalTris, const glm::vec3& relPos)
{
    meshGPU.draw(totalTris, relPos, false); // Disable directional culling for testing
}

/**
 * Helper to query padding masks taking active LOD step size into account.
 */
inline bool Chunk::isPaddingSolid(const PaddingMasks& pad, int axis, int dir, int i, int j, int LOD_StepSize) const
{
    int baseI = i * LOD_StepSize;
    int baseJ = j * LOD_StepSize;

    uint16_t lodMask = ((1u << LOD_StepSize) - 1u) << baseJ;

    for (int subI = 0; subI < LOD_StepSize; ++subI) {
        int yy = baseI + subI;
        if (yy >= CHUNK_SIZE) break;

        uint16_t row = 0;
        if (axis == 0)      row = (dir < 0) ? pad.xNeg[yy] : pad.xPos[yy];
        else if (axis == 1) row = (dir < 0) ? pad.yNeg[yy] : pad.yPos[yy];
        else                row = (dir < 0) ? pad.zNeg[yy] : pad.zPos[yy];

        // Cull ONLY if the entire mask is solid
        if ((row & lodMask) != lodMask) return false;
    }
    return true;
}

inline bool Chunk::neighborSolid(const PaddingMasks& pad, int axis, int dir, int x, int y, int z)
{
    auto inRange = [](int v) { return v >= 0 && v < CHUNK_SIZE; };
    auto clamp = [](int v) { if (v < 0) return 0; if (v >= CHUNK_SIZE) return CHUNK_SIZE - 1; return v; };

    if (axis == 0) {
        int targetX = x + dir;
        if (!inRange(targetX)) {
            int yy = clamp(y), zz = clamp(z);
            return (targetX < 0) ? (pad.xNeg[yy] & (1u << zz)) : (pad.xPos[yy] & (1u << zz));
        }
        if (!inRange(y) || !inRange(z)) return false;
        return !blocks[targetX][y][z].isAir();
    }
    else if (axis == 1) {
        int targetY = y + dir;
        if (!inRange(targetY)) {
            int xx = clamp(x), zz = clamp(z);
            return (targetY < 0) ? (pad.yNeg[xx] & (1u << zz)) : (pad.yPos[xx] & (1u << zz));
        }
        if (!inRange(x) || !inRange(z)) return false;
        return !blocks[x][targetY][z].isAir();
    }
    else { // axis == 2
        int targetZ = z + dir;
        if (!inRange(targetZ)) {
            int xx = clamp(x), yy = clamp(y);
            return (targetZ < 0) ? (pad.zNeg[xx] & (1u << yy)) : (pad.zPos[xx] & (1u << yy));
        }
        if (!inRange(x) || !inRange(y)) return false;
        return !blocks[x][y][targetZ].isAir();
    }
}

void Chunk::buildGreedyMesh(const glm::vec3& lightDir, const int LOD)
{
    clearMesh();
    buildGreedyMeshInternal(lightDir, meshCPU, LOD);
}

/**
 * Thread-safe variant of mesh building that outputs to a provided mesh.
 * Can be called from worker threads without accessing World.
 */
void Chunk::buildGreedyMeshThreadSafe(const glm::vec3& lightDir, ChunkMesh& outMesh, const int LOD)
{
    outMesh.clear();
    buildGreedyMeshInternal(lightDir, outMesh, LOD);
}

/**
 * OPTIMIZED GREEDY MESHING ALGORITHM (WITH VOXEL MIPMAPPING)
 */
void Chunk::buildGreedyMeshInternal(const glm::vec3& lightDir, ChunkMesh& outMesh, const int LOD)
{
    NeighbourLODs neighbours = this->LODs;
    const int LOD_StepSize = LOD;

    // Dynamic allocation check
    for (int i = 0; i < 6; ++i) {
        if (outMesh.vertices[i].capacity() < MESH_VERTEX_RESERVE / 6)
            outMesh.vertices[i].reserve(MESH_VERTEX_RESERVE / 6);
        if (outMesh.indices[i].capacity() < MESH_INDEX_RESERVE / 6)
            outMesh.indices[i].reserve(MESH_INDEX_RESERVE / 6);
    }

    // The working size of our grid changes depending on the LOD level
    const int LOD_ChunkSize = CHUNK_SIZE / LOD_StepSize;

    // Bitmasks matched precisely to a maximum size of 16
    uint16_t mask[16];
    uint8_t  type[16][16];
    uint8_t  light[16][16];

    // Vertex count is outside
    uint32_t totalVertexCount = 0;

    for (int face = 0; face < 6; ++face)
    {
        const FaceConfig& faceConfig = FACES[face];
        const int axis = faceConfig.axis;
        const int dir = faceConfig.dir;

        int neighborStepSize = 1;
        std::array<int, 6> nLOD = neighbours.getNeighbourLODs();

        if (axis == 0) neighborStepSize = (dir > 0) ? nLOD[0] : nLOD[1];
        else if (axis == 1) neighborStepSize = (dir > 0) ? nLOD[2] : nLOD[3];
        else                neighborStepSize = (dir > 0) ? nLOD[4] : nLOD[5];

        // Process each slice along active axis
        for (int sliceDepth = 0; sliceDepth < LOD_ChunkSize; ++sliceDepth)
        {
            // Reset mask for current slice
            for (int i = 0; i < LOD_ChunkSize; ++i) mask[i] = 0;

            const int targetSlice = sliceDepth + dir;

            for (int i = 0; i < LOD_ChunkSize; ++i)
            {
                for (int j = 0; j < LOD_ChunkSize; ++j)
                {
                    const int blockAlongAxis = sliceDepth;

                    // Fetch active block at current LOD
                    _Block block;
                    if (LOD_StepSize == 1) {
                        block = blocks[axis == 0 ? blockAlongAxis : i][axis == 1 ? blockAlongAxis : (axis == 0 ? i : j)][axis == 2 ? blockAlongAxis : j];
                    }
                    else if (LOD_StepSize == 2) {
                        block = blocksLOD2[axis == 0 ? blockAlongAxis : i][axis == 1 ? blockAlongAxis : (axis == 0 ? i : j)][axis == 2 ? blockAlongAxis : j];
                    }
                    else if (LOD_StepSize == 4) {
                        block = blocksLOD3[axis == 0 ? blockAlongAxis : i][axis == 1 ? blockAlongAxis : (axis == 0 ? i : j)][axis == 2 ? blockAlongAxis : j];
                    }
                    else if (LOD_StepSize == 8) {
                        block = blocksLOD4[axis == 0 ? blockAlongAxis : i][axis == 1 ? blockAlongAxis : (axis == 0 ? i : j)][axis == 2 ? blockAlongAxis : j];
                    }
                    else { // LOD_StepSize == 16
                        block = blockLOD5;
                    }

                    if (block.isAir()) continue;

                    // Occlusion Check: is neighbor in direction 'dir' solid?
                    bool neighborIsSolid = false;

                    if (targetSlice < 0 || targetSlice >= LOD_ChunkSize)
                    {
                        // Check boundary padding
                        neighborIsSolid = isPaddingSolid(padding, axis, dir, i, j, LOD_StepSize);
                    }
                    else
                    {
                        // Check internal adjacent voxel within the active LOD level
                        _Block nbrBlock;
                        if (LOD_StepSize == 1) {
                            nbrBlock = blocks[axis == 0 ? targetSlice : i][axis == 1 ? targetSlice : (axis == 0 ? i : j)][axis == 2 ? targetSlice : j];
                        }
                        else if (LOD_StepSize == 2) {
                            nbrBlock = blocksLOD2[axis == 0 ? targetSlice : i][axis == 1 ? targetSlice : (axis == 0 ? i : j)][axis == 2 ? targetSlice : j];
                        }
                        else if (LOD_StepSize == 4) {
                            nbrBlock = blocksLOD3[axis == 0 ? targetSlice : i][axis == 1 ? targetSlice : (axis == 0 ? i : j)][axis == 2 ? targetSlice : j];
                        }
                        else if (LOD_StepSize == 8) {
                            nbrBlock = blocksLOD4[axis == 0 ? targetSlice : i][axis == 1 ? targetSlice : (axis == 0 ? i : j)][axis == 2 ? targetSlice : j];
                        }
                        else {
                            nbrBlock = blockLOD5;
                        }
                        neighborIsSolid = !nbrBlock.isAir();
                    }

                    if (forceAllFaces || !neighborIsSolid)
                    {
                        mask[i] |= (1u << j);
                        type[i][j] = block.getType();
                        light[i][j] = block.getLight() == 0 ? DEFAULT_LIGHT_LEVEL : block.getLight();
                    }
                }
            }

            // Greedy Meshing Engine
            for (int uCoord = 0; uCoord < LOD_ChunkSize; ++uCoord)
            {
                while (mask[uCoord])
                {
                    int vCoord = std::countr_zero(mask[uCoord]);
                    int maxPossibleWidth = LOD_ChunkSize - uCoord;
                    int width = 1;

                    while (width < maxPossibleWidth && (mask[uCoord + width] & (1u << vCoord)) && type[uCoord + width][vCoord] == type[uCoord][vCoord])
                    {
                        ++width;
                    }

                    int maxPossibleHeight = LOD_ChunkSize - vCoord;
                    int height = 1;
                    bool canExpandHeight = false;

                    while (height < maxPossibleHeight && !canExpandHeight)
                    {
                        for (int widthIdx = 0; widthIdx < width; ++widthIdx)
                        {
                            int nextRowV = vCoord + height;
                            bool bitIsSet = mask[uCoord + widthIdx] & (1u << nextRowV);
                            bool typeMatches = type[uCoord + widthIdx][nextRowV] == type[uCoord][vCoord];

                            if (!bitIsSet || !typeMatches)
                            {
                                canExpandHeight = true;
                                break;
                            }
                        }
                        if (!canExpandHeight) ++height;
                    }

                    uint16_t clearBits = ((1u << height) - 1u) << vCoord;
                    for (int widthIdx = 0; widthIdx < width; ++widthIdx)
                    {
                        mask[uCoord + widthIdx] &= ~clearBits;
                    }

                    // Map local LOD metrics back up to global physical world coordinates
                    float facePos = (dir > 0) ? static_cast<float>((sliceDepth + 1) * LOD_StepSize) : static_cast<float>(sliceDepth * LOD_StepSize);
                    float realU = static_cast<float>(uCoord * LOD_StepSize);
                    float realV = static_cast<float>(vCoord * LOD_StepSize);
                    float realW = static_cast<float>(width * LOD_StepSize);
                    float realH = static_cast<float>(height * LOD_StepSize);

                    glm::vec3 origin(0.0f);
                    glm::vec3 du(0.0f);
                    glm::vec3 dv(0.0f);

                    // Calculate quad dimensions based on face orientation
                    if (axis == 0) // X faces (YZ plane)
                    {
                        origin = glm::vec3(facePos, float(realU), float(realV));
                        du = glm::vec3(0.0f, float(realW), 0.0f);
                        dv = glm::vec3(0.0f, 0.0f, float(realH));
                    }
                    else if (axis == 1) // Y faces (XZ plane)
                    {
                        origin = glm::vec3(float(realU), facePos, float(realV));
                        du = glm::vec3(float(realW), 0.0f, 0.0f);
                        dv = glm::vec3(0.0f, 0.0f, float(realH));
                    }
                    else // Z faces (XY plane)
                    {
                        origin = glm::vec3(float(realU), float(realV), facePos);
                        du = glm::vec3(float(realW), 0.0f, 0.0f);
                        dv = glm::vec3(0.0f, float(realH), 0.0f);
                    }

                    // Calculate UV scale based on quad dimensions
                    float uScale = (axis == 0) ? float(realH) : float(realW);
                    float vScale = (axis == 0) ? float(realW) : float(realH);
                    glm::vec2 scale(uScale, vScale);

                    /*
                    bool isOnBoundary = (dir > 0 && sliceDepth == LOD_ChunkSize - 1) || (dir < 0 && sliceDepth == 0);
                    if (isOnBoundary && neighborStepSize > LOD_StepSize)
                    {
                        float skirtExtrusion = 0.05f * LOD_StepSize;
                        origin -= (glm::vec3(FACES[face].normal) * skirtExtrusion);
                    }

                    glm::vec2 scale(uScale, vScale);
                    */

                    int texLayer = static_cast<int>(BLOCK_FACE_TEXTURE[type[uCoord][vCoord]][face]);
                    int aoVal = static_cast<int>(light[uCoord][vCoord]);
                    uint32_t base = totalVertexCount;

                    Vertex v1, v2, v3, v4;
                    v1 = v1.packVertex(origin, glm::vec2(FACE_UVS[face][0]) * scale, aoVal, texLayer, face);
                    v2 = v2.packVertex(origin + dv, glm::vec2(FACE_UVS[face][1]) * scale, aoVal, texLayer, face);
                    v3 = v3.packVertex(origin + dv + du, glm::vec2(FACE_UVS[face][2]) * scale, aoVal, texLayer, face);
                    v4 = v4.packVertex(origin + du, glm::vec2(FACE_UVS[face][3]) * scale, aoVal, texLayer, face);

                    outMesh.vertices[face].push_back(v1);
                    outMesh.vertices[face].push_back(v2);
                    outMesh.vertices[face].push_back(v3);
                    outMesh.vertices[face].push_back(v4);

                    totalVertexCount += 4;

                    glm::vec3 quadNormal = glm::cross(du, dv);
                    bool flip = glm::dot(quadNormal, glm::vec3(FACES[face].normal)) < 0.0f;
                    if (flip) {
                        outMesh.indices[face].push_back(base + 0); outMesh.indices[face].push_back(base + 1); outMesh.indices[face].push_back(base + 2);
                        outMesh.indices[face].push_back(base + 2); outMesh.indices[face].push_back(base + 3); outMesh.indices[face].push_back(base + 0);
                    }
                    else {
                        outMesh.indices[face].push_back(base + 0); outMesh.indices[face].push_back(base + 2); outMesh.indices[face].push_back(base + 1);
                        outMesh.indices[face].push_back(base + 0); outMesh.indices[face].push_back(base + 3); outMesh.indices[face].push_back(base + 2);
                    }
                }
            }
        }
    }
}

int Chunk::getDistanceFromPlayerSquared(const glm::dvec3& playerPos, const ChunkCoord& coord) {

    glm::dvec3 chunkPosVec = { (double)coord.x, (double)coord.y, (double)coord.z };
    glm::dvec3 diff = playerPos / (double)CHUNK_SIZE - chunkPosVec;

    return (int)(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
}

int Chunk::getLODlevel(const int& LOD_DistanceSquared) {
    int LOD_StepSize = 1; // Default fallback
    if (LOD_DistanceSquared > MAX_LOD_RADIUS_SQUARED) {
        auto it = std::upper_bound(std::begin(CHUNK_LOD_LEVEL_DISTANCES), std::end(CHUNK_LOD_LEVEL_DISTANCES), LOD_DistanceSquared, [](int value, const auto& entry)
            {
                return value < entry.first;
            }
        );

        if (it != std::begin(CHUNK_LOD_LEVEL_DISTANCES)) {
            --it;
            LOD_StepSize = it->second;
        }
        else {
            LOD_StepSize = CHUNK_LOD_LEVEL_DISTANCES[0].second;
        }
    }
    else {
        LOD_StepSize = 1;
    }
    return LOD_StepSize;
}

inline bool Chunk::isSolidSafe(const Chunk* c, int x, int y, int z)
{
    if (!c) return false;
    if (x < 0 || y < 0 || z < 0 || x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) return false;

    return !c->blocks[x][y][z].isAir();
}

void Chunk::rebuildPadding()
{
    World* w = owner;

    ChunkCoord nxp = { chunkX + 1, chunkY, chunkZ };
    ChunkCoord nxn = { chunkX - 1, chunkY, chunkZ };
    ChunkCoord nyp = { chunkX, chunkY + 1, chunkZ };
    ChunkCoord nyn = { chunkX, chunkY - 1, chunkZ };
    ChunkCoord nzp = { chunkX, chunkY, chunkZ + 1 };
    ChunkCoord nzn = { chunkX, chunkY, chunkZ - 1 };

    Chunk* cxp = w->getChunk(nxp);
    Chunk* cxn = w->getChunk(nxn);
    Chunk* cyp = w->getChunk(nyp);
    Chunk* cyn = w->getChunk(nyn);
    Chunk* czp = w->getChunk(nzp);
    Chunk* czn = w->getChunk(nzn);

    bool sxp = (!cxp) && w->isChunkHiddenSolid(nxp);
    bool sxn = (!cxn) && w->isChunkHiddenSolid(nxn);
    bool syp = (!cyp) && w->isChunkHiddenSolid(nyp);
    bool syn = (!cyn) && w->isChunkHiddenSolid(nyn);
    bool szp = (!czp) && w->isChunkHiddenSolid(nzp);
    bool szn = (!czn) && w->isChunkHiddenSolid(nzn);

    for (int i = 0; i < CHUNK_SIZE; ++i)
    {
        uint16_t rxp = 0, rxn = 0;
        uint16_t ryp = 0, ryn = 0;
        uint16_t rzp = 0, rzn = 0;

        for (int j = 0; j < CHUNK_SIZE; ++j)
        {
            if (cxp ? isSolidSafe(cxp, 0, i, j) : sxp) rxp |= (1u << j);
            if (cxn ? isSolidSafe(cxn, CHUNK_SIZE - 1, i, j) : sxn) rxn |= (1u << j);

            if (cyp ? isSolidSafe(cyp, i, 0, j) : syp) ryp |= (1u << j);
            if (cyn ? isSolidSafe(cyn, i, CHUNK_SIZE - 1, j) : syn) ryn |= (1u << j);

            if (czp ? isSolidSafe(czp, i, j, 0) : szp) rzp |= (1u << j);
            if (czn ? isSolidSafe(czn, i, j, CHUNK_SIZE - 1) : szn) rzn |= (1u << j);
        }

        padding.xPos[i] = rxp;
        padding.xNeg[i] = rxn;
        padding.yPos[i] = ryp;
        padding.yNeg[i] = ryn;
        padding.zPos[i] = rzp;
        padding.zNeg[i] = rzn;
    }
}

void Chunk::rebuildPaddingFromSnapshot(
    const std::array<PaddingMasks, 6>& neighborPadding,
    const std::array<bool, 6>& neighborExists,
    const std::array<bool, 6>& neighborHiddenSolid
)
{
    bool sxp = (!neighborExists[0]) && neighborHiddenSolid[0];
    bool sxn = (!neighborExists[1]) && neighborHiddenSolid[1];
    bool syp = (!neighborExists[2]) && neighborHiddenSolid[2];
    bool syn = (!neighborExists[3]) && neighborHiddenSolid[3];
    bool szp = (!neighborExists[4]) && neighborHiddenSolid[4];
    bool szn = (!neighborExists[5]) && neighborHiddenSolid[5];

    for (int i = 0; i < CHUNK_SIZE; ++i)
    {
        uint16_t rxp = 0, rxn = 0;
        uint16_t ryp = 0, ryn = 0;
        uint16_t rzp = 0, rzn = 0;

        for (int j = 0; j < CHUNK_SIZE; ++j)
        {
            if (neighborExists[0]) {
                if (neighborPadding[0].xNeg[i] & (1u << j)) rxp |= (1u << j);
            }
            else if (sxp) {
                rxp |= (1u << j);
            }

            if (neighborExists[1]) {
                if (neighborPadding[1].xPos[i] & (1u << j)) rxn |= (1u << j);
            }
            else if (sxn) {
                rxn |= (1u << j);
            }

            if (neighborExists[2]) {
                if (neighborPadding[2].yNeg[i] & (1u << j)) ryp |= (1u << j);
            }
            else if (syp) {
                ryp |= (1u << j);
            }

            if (neighborExists[3]) {
                if (neighborPadding[3].yPos[i] & (1u << j)) ryn |= (1u << j);
            }
            else if (syn) {
                ryn |= (1u << j);
            }

            if (neighborExists[4]) {
                if (neighborPadding[4].zNeg[i] & (1u << j)) rzp |= (1u << j);
            }
            else if (szp) {
                rzp |= (1u << j);
            }

            if (neighborExists[5]) {
                if (neighborPadding[5].zPos[i] & (1u << j)) rzn |= (1u << j);
            }
            else if (szn) {
                rzn |= (1u << j);
            }
        }

        padding.xPos[i] = rxp;
        padding.xNeg[i] = rxn;
        padding.yPos[i] = ryp;
        padding.yNeg[i] = ryn;
        padding.zPos[i] = rzp;
        padding.zNeg[i] = rzn;
    }
}

PaddingMasks Chunk::getPaddingDataForNeighbor(int neighborIndex) const
{
    PaddingMasks result;
    std::memset(&result, 0, sizeof(result));

    switch (neighborIndex) {
    case 0:
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            for (int j = 0; j < CHUNK_SIZE; ++j) {
                if (!blocks[0][i][j].isAir()) {
                    result.xNeg[i] |= (1u << j);
                }
            }
        }
        break;
    case 1:
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            for (int j = 0; j < CHUNK_SIZE; ++j) {
                if (!blocks[CHUNK_SIZE - 1][i][j].isAir()) {
                    result.xPos[i] |= (1u << j);
                }
            }
        }
        break;
    case 2:
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            for (int j = 0; j < CHUNK_SIZE; ++j) {
                if (!blocks[i][0][j].isAir()) {
                    result.yNeg[i] |= (1u << j);
                }
            }
        }
        break;
    case 3:
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            for (int j = 0; j < CHUNK_SIZE; ++j) {
                if (!blocks[i][CHUNK_SIZE - 1][j].isAir()) {
                    result.yPos[i] |= (1u << j);
                }
            }
        }
        break;
    case 4:
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            for (int j = 0; j < CHUNK_SIZE; ++j) {
                if (!blocks[i][j][0].isAir()) {
                    result.zNeg[i] |= (1u << j);
                }
            }
        }
        break;
    case 5:
        for (int i = 0; i < CHUNK_SIZE; ++i) {
            for (int j = 0; j < CHUNK_SIZE; ++j) {
                if (!blocks[i][j][CHUNK_SIZE - 1].isAir()) {
                    result.zPos[i] |= (1u << j);
                }
            }
        }
        break;
    }

    return result;
}

void Chunk::rebuildNeighbourLODs(const glm::dvec3& playerPos) {

    World* w = owner;
    if (!w) return;

    ChunkCoord nxp = { chunkX + 1, chunkY, chunkZ };
    ChunkCoord nxn = { chunkX - 1, chunkY, chunkZ };
    ChunkCoord nyp = { chunkX, chunkY + 1, chunkZ };
    ChunkCoord nyn = { chunkX, chunkY - 1, chunkZ };
    ChunkCoord nzp = { chunkX, chunkY, chunkZ + 1 };
    ChunkCoord nzn = { chunkX, chunkY, chunkZ - 1 };

    Chunk* cxp = w->getChunk(nxp);
    Chunk* cxn = w->getChunk(nxn);
    Chunk* cyp = w->getChunk(nyp);
    Chunk* cyn = w->getChunk(nyn);
    Chunk* czp = w->getChunk(nzp);
    Chunk* czn = w->getChunk(nzn);

    int lxp = (cxp) ? getLODlevel(getDistanceFromPlayerSquared(playerPos, nxp)) : -1;
    int lxn = (cxn) ? getLODlevel(getDistanceFromPlayerSquared(playerPos, nxn)) : -1;
    int lyp = (cyp) ? getLODlevel(getDistanceFromPlayerSquared(playerPos, nyp)) : -1;
    int lyn = (cyn) ? getLODlevel(getDistanceFromPlayerSquared(playerPos, nyn)) : -1;
    int lzp = (czp) ? getLODlevel(getDistanceFromPlayerSquared(playerPos, nzp)) : -1;
    int lzn = (czn) ? getLODlevel(getDistanceFromPlayerSquared(playerPos, nzn)) : -1;

    this->LODs.setNeighbourLODs(lxp, lxn, lyp, lyn, lzp, lzn);
}

void Chunk::rebuildNeighbourLODsFromSnapshot(const std::array<bool, 6>& neighborExists, const NeighbourLODs& neighborLODs)
{
    std::array<int, 6> temp;
    std::array<int, 6> LODs = neighborLODs.getNeighbourLODs();

    for (int i = 0; i < 6; i++) {
        if (neighborExists[i]) {
            temp[i] = LODs[i];
        }
        else {
            temp[i] = -1;
        }
    }

    this->LODs.setNeighbourLODs(
        temp[0],
        temp[1],
        temp[2],
        temp[3],
        temp[4],
        temp[5]
    );
}

_Block Chunk::GetMostFrequentBlock(const _Block cluster[8]) {
    BlockCount counts[8];
    int uniqueTypes = 0;

    int maxCount = 0;
    _Block mostFrequent = cluster[0];

    int grassCount = 0;
    int dirtCount = 0;
    int stoneCount = 0;
    int airCount = 0;

    for (int i = 0; i < 8; ++i) {
        int id = cluster[i].getType();

        if (id == BlockType::GRASS) grassCount++;
        else if (id == BlockType::DIRT) dirtCount++;
        else if (id == BlockType::STONE) stoneCount++;
        else if (id == BlockType::AIR) airCount++;

        bool found = false;
        for (int j = 0; j < uniqueTypes; ++j) {
            if (counts[j].typeId == id) {
                counts[j].count++;
                if (counts[j].count > maxCount) {
                    maxCount = counts[j].count;
                    mostFrequent = cluster[i];
                }
                found = true;
                break;
            }
        }

        if (!found) {
            counts[uniqueTypes] = { id, 1 };
            if (maxCount == 0) {
                maxCount = 1;
                mostFrequent = cluster[i];
            }
            uniqueTypes++;
        }
    }

    if (grassCount > 1 && (dirtCount >= 4 || stoneCount >= 4)) {
        mostFrequent.setType(BlockType::GRASS);
        return mostFrequent;
    }

    if (airCount > 6) {
        mostFrequent.setType(BlockType::AIR);
    }

    return mostFrequent;
}

void Chunk::rebuildBlockMipmaps() {
    _Block cluster[8];

    // 1. Generate LOD2 (8x8x8) from LOD1 (16x16x16)
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            for (int z = 0; z < 8; ++z) {
                int srcX = x * 2, srcY = y * 2, srcZ = z * 2;

                cluster[0] = blocks[srcX][srcY][srcZ];
                cluster[1] = blocks[srcX + 1][srcY][srcZ];
                cluster[2] = blocks[srcX][srcY + 1][srcZ];
                cluster[3] = blocks[srcX + 1][srcY + 1][srcZ];
                cluster[4] = blocks[srcX][srcY][srcZ + 1];
                cluster[5] = blocks[srcX + 1][srcY][srcZ + 1];
                cluster[6] = blocks[srcX][srcY + 1][srcZ + 1];
                cluster[7] = blocks[srcX + 1][srcY + 1][srcZ + 1];

                blocksLOD2[x][y][z] = GetMostFrequentBlock(cluster);
            }
        }
    }

    // 2. Generate LOD3 (4x4x4) from LOD2 (8x8x8)
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 4; ++y) {
            for (int z = 0; z < 4; ++z) {
                int srcX = x * 2, srcY = y * 2, srcZ = z * 2;
                cluster[0] = blocksLOD2[srcX][srcY][srcZ];
                cluster[1] = blocksLOD2[srcX + 1][srcY][srcZ];
                cluster[2] = blocksLOD2[srcX][srcY + 1][srcZ];
                cluster[3] = blocksLOD2[srcX + 1][srcY + 1][srcZ];
                cluster[4] = blocksLOD2[srcX][srcY][srcZ + 1];
                cluster[5] = blocksLOD2[srcX + 1][srcY][srcZ + 1];
                cluster[6] = blocksLOD2[srcX][srcY + 1][srcZ + 1];
                cluster[7] = blocksLOD2[srcX + 1][srcY + 1][srcZ + 1];

                blocksLOD3[x][y][z] = GetMostFrequentBlock(cluster);
            }
        }
    }

    // 3. Generate LOD4 (2x2x2) from LOD3 (4x4x4)
    for (int x = 0; x < 2; ++x) {
        for (int y = 0; y < 2; ++y) {
            for (int z = 0; z < 2; ++z) {
                int srcX = x * 2, srcY = y * 2, srcZ = z * 2;
                cluster[0] = blocksLOD3[srcX][srcY][srcZ];
                cluster[1] = blocksLOD3[srcX + 1][srcY][srcZ];
                cluster[2] = blocksLOD3[srcX][srcY + 1][srcZ];
                cluster[3] = blocksLOD3[srcX + 1][srcY + 1][srcZ];
                cluster[4] = blocksLOD3[srcX][srcY][srcZ + 1];
                cluster[5] = blocksLOD3[srcX + 1][srcY][srcZ + 1];
                cluster[6] = blocksLOD3[srcX][srcY + 1][srcZ + 1];
                cluster[7] = blocksLOD3[srcX + 1][srcY + 1][srcZ + 1];

                blocksLOD4[x][y][z] = GetMostFrequentBlock(cluster);
            }
        }
    }

    // 4. Generate LOD5 (1x1x1) from LOD4 (2x2x2)
    cluster[0] = blocksLOD4[0][0][0];
    cluster[1] = blocksLOD4[1][0][0];
    cluster[2] = blocksLOD4[0][1][0];
    cluster[3] = blocksLOD4[1][1][0];
    cluster[4] = blocksLOD4[0][0][1];
    cluster[5] = blocksLOD4[1][0][1];
    cluster[6] = blocksLOD4[0][1][1];
    cluster[7] = blocksLOD4[1][1][1];

    blockLOD5 = GetMostFrequentBlock(cluster);
}

void Chunk::applyMesh(ChunkMesh&& mesh)
{
    meshCPU = std::move(mesh);
}