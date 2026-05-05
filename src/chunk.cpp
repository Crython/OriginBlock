/*
 * CHUNK.CPP
 * 
 * Manages individual chunk operations including:
 * - Terrain generation and block storage
 * - Greedy mesh generation for efficient rendering
 * - Neighbor padding system for seamless chunk boundaries
 * - Dirty state tracking for incremental updates
 * 
 * The greedy meshing algorithm combines adjacent faces with the same texture/orientation
 * into larger quads to minimize draw calls. Neighbor padding allows chunks to mesh
 * independently by storing adjacent chunk boundary information.
 */

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
}

void Chunk::generateTerrain(int seed)
{
    Terrain::generate({ chunkX, chunkY, chunkZ }, seed, blocks);
    countSolidBlocks();
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
    meshGPU.draw(totalTris, relPos, true); // Enable robust directional culling
}

inline bool Chunk::neighborSolid(const PaddingMasks& pad, int axis, int dir, int x, int y, int z)
{
    if (axis == 0)
    {
        // X faces -> YZ plane
        if (x == 0 && dir == -1) return pad.xNeg[y] & (1u << z);
        if (x == CHUNK_SIZE - 1 && dir == +1) return pad.xPos[y] & (1u << z);
        return !blocks[x + dir][y][z].isAir();
    }
    else if (axis == 1)
    {
        // Y faces -> XZ plane
        if (y == 0 && dir == -1) return pad.yNeg[x] & (1u << z);
        if (y == CHUNK_SIZE - 1 && dir == +1) return pad.yPos[x] & (1u << z);
        return !blocks[x][y + dir][z].isAir();
    }
    else
    {
        // Z faces -> XY plane
        if (z == 0 && dir == -1) return pad.zNeg[x] & (1u << y);
        if (z == CHUNK_SIZE - 1 && dir == +1) return pad.zPos[x] & (1u << y);
        return !blocks[x][y][z + dir].isAir();
    }
}

void Chunk::buildGreedyMesh(const glm::vec3& lightDir)
{
    clearMesh();
    buildGreedyMeshInternal(lightDir, meshCPU);
}

/**
 * Thread-safe variant of mesh building that outputs to a provided mesh.
 * Can be called from worker threads without accessing World.
 */
void Chunk::buildGreedyMeshThreadSafe(const glm::vec3& lightDir, ChunkMesh& outMesh)
{
    outMesh.clear();
    buildGreedyMeshInternal(lightDir, outMesh);
}

/**
 * GREEDY MESHING ALGORITHM
 * 
 * Combines adjacent faces with the same texture into larger quads to reduce draw calls.
 * Algorithm:
 * 1. For each face direction (6 total)
 * 2. For each slice perpendicular to that direction (17 slices including boundaries)
 * 3. Build a visibility mask showing which blocks expose a face
 * 4. Greedily merge adjacent exposed faces into rectangular quads
 * 5. Emit vertices with packed position, UV, normal, and lighting data
 */
void Chunk::buildGreedyMeshInternal(const glm::vec3& lightDir, ChunkMesh& outMesh)
{
    // Reserve space based on typical chunk mesh sizes
    for (int i = 0; i < 6; ++i) {
        outMesh.vertices[i].reserve(MESH_VERTEX_RESERVE / 6);
        outMesh.indices[i].reserve(MESH_INDEX_RESERVE / 6);
    }

    // Working arrays for greedy meshing
    uint16_t mask[CHUNK_SIZE];              // Bitmask of visible faces in current slice
    uint8_t  type[CHUNK_SIZE][CHUNK_SIZE];  // Block types for texture lookup
    uint8_t  light[CHUNK_SIZE][CHUNK_SIZE]; // Light values for shading

    uint32_t totalVertexCount = 0; // Global vertex counter for all faces

    // Process each of the 6 face directions (±X, ±Y, ±Z)
    for (int face = 0; face < 6; ++face)
    {
        /*
		* face 0: +X
		* face 1: -X
		* face 2: +Y
		* face 3: -Y
		* face 4: +Z
		* face 5: -Z
        */


        const FaceConfig& faceConfig = FACES[face];
        const int axis = faceConfig.axis;
        const int dir = faceConfig.dir;

        // Loop from 0 to CHUNK_SIZE inclusive (17 iterations)
        // This includes boundary slices where faces between chunks are generated
        for (int sliceDepth = 0; sliceDepth <= CHUNK_SIZE; ++sliceDepth)
        {
            // Reset mask
            for (int i = 0; i < CHUNK_SIZE; ++i) mask[i] = 0;

            // Build visibility mask
            for (int i = 0; i < CHUNK_SIZE; ++i)
            {
                for (int j = 0; j < CHUNK_SIZE; ++j)
                {
                    // Determine which block owns this potential face
                    int blockAlongAxis = (dir > 0) ? (sliceDepth - 1) : sliceDepth;

                    // Skip invalid blocks
                    if (blockAlongAxis < 0 || blockAlongAxis >= CHUNK_SIZE) continue;

                    int bx, by, bz;
                    if (axis == 0) { bx = blockAlongAxis; by = i; bz = j; }
                    else if (axis == 1) { bx = i; by = blockAlongAxis; bz = j; }
                    else { bx = i; by = j; bz = blockAlongAxis; }

                    const _Block& curr = blocks[bx][by][bz];
                    if (curr.isAir()) continue;

                    // Face is exposed if neighbor in direction is air
                    if (!neighborSolid(padding, axis, dir, bx, by, bz))
                    {
                        mask[i] |= (1u << j);
                        type[i][j] = curr.getType();
                        light[i][j] = curr.getLight() == 0 ? DEFAULT_LIGHT_LEVEL : curr.getLight();
                    }
                }
            }

            // GREEDY MERGING - Find maximal rectangular quads
            for (int uCoord = 0; uCoord < CHUNK_SIZE; ++uCoord)
            {
                while (mask[uCoord])
                {
                    // Find the first set bit (lowest exposed face)
                    int vCoord = std::countr_zero(mask[uCoord]);

                    // Expand in width (u direction) while faces match
                    int maxPossibleWidth = CHUNK_SIZE - uCoord;
                    int width = 1;
                    while (width < maxPossibleWidth && (mask[uCoord + width] & (1u << vCoord)) && type[uCoord + width][vCoord] == type[uCoord][vCoord])
                    {
                        ++width;
                    }

                    // Expand in height (v direction) while entire rows match
                    int maxPossibleHeight = CHUNK_SIZE - vCoord;
                    int height = 1;
                    bool canExpandHeight = false;
                    while (height < maxPossibleHeight && !canExpandHeight)
                    {
                        // Check if entire width can expand by one more row
                        for (int widthIdx = 0; widthIdx < width; ++widthIdx)
                        {
                            if (!(mask[uCoord + widthIdx] & (1u << (vCoord + height))) || type[uCoord + widthIdx][vCoord + height] != type[uCoord][vCoord])
                            {
                                canExpandHeight = true;
                                break;
                            }
                        }
                        if (!canExpandHeight) ++height;
                    }

                    // Clear the merged bits from the mask
                    uint16_t clearBits = ((1u << height) - 1u) << vCoord;
                    for (int widthIdx = 0; widthIdx < width; ++widthIdx)
                        mask[uCoord + widthIdx] &= ~clearBits;

                    // EMIT QUAD - Create vertices for the merged rectangular face
                    float facePos = static_cast<float>(sliceDepth);

                    glm::vec3 origin(0.0f);
                    glm::vec3 du(0.0f);
                    glm::vec3 dv(0.0f);

                    // Calculate quad dimensions based on face orientation
                    if (axis == 0) // X faces (YZ plane)
                    {
                        origin = glm::vec3(facePos, float(uCoord), float(vCoord));
                        du = glm::vec3(0.0f, float(width), 0.0f);
                        dv = glm::vec3(0.0f, 0.0f, float(height));
                    }
                    else if (axis == 1) // Y faces (XZ plane)
                    {
                        origin = glm::vec3(float(uCoord), facePos, float(vCoord));
                        du = glm::vec3(float(width), 0.0f, 0.0f);
                        dv = glm::vec3(0.0f, 0.0f, float(height));
                    }
                    else // Z faces (XY plane)
                    {
                        origin = glm::vec3(float(uCoord), float(vCoord), facePos);
                        du = glm::vec3(float(width), 0.0f, 0.0f);
                        dv = glm::vec3(0.0f, float(height), 0.0f);
                    }

                    // Emit quad vertices using packed vertex format
                    uint32_t base = totalVertexCount;
                    
                    // Pack vertex data (position, UV, normal, AO) into compact format
                    auto packVertex = [&](glm::vec3 pos, glm::vec2 uv, int aoVal, int texLayer) {
                        Vertex v;

						// Ensure values fit in their bit ranges (5 bits = 0-31)
                        assert(pos.x >= 0 && pos.x < 32 && "X out of 5-bit range");
                        assert(pos.y >= 0 && pos.y < 32 && "Y out of 5-bit range");
                        assert(pos.z >= 0 && pos.z < 32 && "Z out of 5-bit range");

                        uint32_t vx = static_cast<uint32_t>(pos.x);
                        uint32_t vy = static_cast<uint32_t>(pos.y);
                        uint32_t vz = static_cast<uint32_t>(pos.z);
                        uint32_t vu = static_cast<uint32_t>(uv.x);
                        uint32_t vv = static_cast<uint32_t>(uv.y);
                        
                        // Pack data1: X(5), Y(5), Z(5), U(5), V(5), Normal(3), AO_low(4) = 32 bits
                        v.data1 = (vx & VERTEX_5BIT_MASK) | 
                                  ((vy & VERTEX_5BIT_MASK) << VERTEX_Y_SHIFT) | 
                                  ((vz & VERTEX_5BIT_MASK) << VERTEX_Z_SHIFT) | 
                                  ((vu & VERTEX_5BIT_MASK) << VERTEX_U_SHIFT) | 
                                  ((vv & VERTEX_5BIT_MASK) << VERTEX_V_SHIFT) |  
                                  ((uint32_t(aoVal) & VERTEX_4BIT_MASK) << VERTEX_AO_LOW_SHIFT);
                        
                        // Pack data2: Texture layer(15 bits) + AO_high(1 bit) = 16 bits
                        uint16_t aoHigh = static_cast<uint16_t>((aoVal >> 4) & 0x1);
                        v.data2 = static_cast<uint16_t>(texLayer & 0x7FFF) | (aoHigh << 15);
                        return v;
                    };

                    // Calculate UV scale based on quad dimensions
                    float uScale = (axis == 0) ? float(height) : float(width);
                    float vScale = (axis == 0) ? float(width) : float(height);
                    glm::vec2 scale(uScale, vScale);

                    int texLayer = static_cast<int>(BLOCK_FACE_TEXTURE[type[uCoord][vCoord]][face]);
                    int aoVal = static_cast<int>(light[uCoord][vCoord]);

                    // Create 4 vertices for the quad (bottom-left, top-left, top-right, bottom-right)
                    outMesh.vertices[face].push_back(packVertex(origin, glm::vec2(FACE_UVS[face][0]) * scale, aoVal, texLayer));
                    outMesh.vertices[face].push_back(packVertex(origin + dv,      glm::vec2(FACE_UVS[face][1]) * scale, aoVal, texLayer));
                    outMesh.vertices[face].push_back(packVertex(origin + dv + du, glm::vec2(FACE_UVS[face][2]) * scale, aoVal, texLayer));
                    outMesh.vertices[face].push_back(packVertex(origin + du,      glm::vec2(FACE_UVS[face][3]) * scale, aoVal, texLayer));

                    totalVertexCount += 4;

                    // Determine winding order to ensure correct face culling
                    glm::vec3 quadNormal = glm::cross(du, dv);
                    bool flip = glm::dot(quadNormal, glm::vec3(FACES[face].normal)) < 0.0f;

                    if (flip)
                    {
                        outMesh.indices[face].push_back(base + 0);
                        outMesh.indices[face].push_back(base + 1);
                        outMesh.indices[face].push_back(base + 2);
                        outMesh.indices[face].push_back(base + 2);
                        outMesh.indices[face].push_back(base + 3);
                        outMesh.indices[face].push_back(base + 0);
                    }
                    else
                    {
                        outMesh.indices[face].push_back(base + 0);
                        outMesh.indices[face].push_back(base + 2);
                        outMesh.indices[face].push_back(base + 1);
                        outMesh.indices[face].push_back(base + 0);
                        outMesh.indices[face].push_back(base + 3);
                        outMesh.indices[face].push_back(base + 2);
                    }
                }
            }
        }
    }
    // Shrink to save memory after greedy meshing is complete
    for (int i = 0; i < 6; ++i) {
        outMesh.vertices[i].shrink_to_fit();
        outMesh.indices[i].shrink_to_fit();
    }
}

// ===========================
// NEIGHBOR PADDING SYSTEM
// ===========================

/**
 * Check if a neighboring block is solid, safely handling chunk boundaries.
 * Used by padding system to determine edge visibility.
 */
inline bool Chunk::isSolidSafe(const Chunk* c, int x, int y, int z)
{
    if (!c) return false; // missing chunk = air
    if (x < 0 || y < 0 || z < 0 || x >= CHUNK_SIZE || y >= CHUNK_SIZE || z >= CHUNK_SIZE) return false;

    return !c->blocks[x][y][z].isAir();
}

/**
 * Rebuild padding masks using neighbor chunk data.
 * 
 * Padding stores which blocks on neighboring chunks are solid, allowing this chunk
 * to generate its mesh independently without querying neighbors during meshing.
 * This is critical for thread-safe mesh generation.
 */
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

    // If neighbor is missing, check if it's a "Hidden Solid" chunk that was unloaded
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

/**
 * Thread-safe padding rebuild using pre-captured neighbor data.
 * 
 * Instead of querying World for neighbors (not thread-safe), this version
 * receives neighbor data as parameters captured on the main thread.
 */
void Chunk::rebuildPaddingFromSnapshot(
    const std::array<PaddingMasks, 6>& neighborPadding,
    const std::array<bool, 6>& neighborExists,
    const std::array<bool, 6>& neighborHiddenSolid
)
{
    // neighborPadding indices: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
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
            // +X neighbor: check their first slice (x=0)
            if (neighborExists[0]) {
                if (neighborPadding[0].xNeg[i] & (1u << j)) rxp |= (1u << j);
            } else if (sxp) {
                rxp |= (1u << j);
            }

            // -X neighbor: check their last slice (x=CHUNK_SIZE-1)
            if (neighborExists[1]) {
                if (neighborPadding[1].xPos[i] & (1u << j)) rxn |= (1u << j);
            } else if (sxn) {
                rxn |= (1u << j);
            }

            // +Y neighbor: check their first slice (y=0)
            if (neighborExists[2]) {
                if (neighborPadding[2].yNeg[i] & (1u << j)) ryp |= (1u << j);
            } else if (syp) {
                ryp |= (1u << j);
            }

            // -Y neighbor: check their last slice (y=CHUNK_SIZE-1)
            if (neighborExists[3]) {
                if (neighborPadding[3].yPos[i] & (1u << j)) ryn |= (1u << j);
            } else if (syn) {
                ryn |= (1u << j);
            }

            // +Z neighbor: check their first slice (z=0)
            if (neighborExists[4]) {
                if (neighborPadding[4].zNeg[i] & (1u << j)) rzp |= (1u << j);
            } else if (szp) {
                rzp |= (1u << j);
            }

            // -Z neighbor: check their last slice (z=CHUNK_SIZE-1)
            if (neighborExists[5]) {
                if (neighborPadding[5].zPos[i] & (1u << j)) rzn |= (1u << j);
            } else if (szn) {
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

/**
 * Get padding data for a specific neighbor direction.
 * 
 * This extracts the outer slice of this chunk's blocks that will be used
 * as padding by the specified neighbor. The neighborIndex maps to:
 * 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
 */
PaddingMasks Chunk::getPaddingDataForNeighbor(int neighborIndex) const
{
    PaddingMasks result;
    std::memset(&result, 0, sizeof(result));

    // Build masks from our block data based on which face the neighbor needs
    switch (neighborIndex) {
        case 0: // We are the +X neighbor. Provide our x=0 slice.
            for (int i = 0; i < CHUNK_SIZE; ++i) {
                for (int j = 0; j < CHUNK_SIZE; ++j) {
                    if (!blocks[0][i][j].isAir()) {
                        result.xNeg[i] |= (1u << j);
                    }
                }
            }
            break;
        case 1: // We are the -X neighbor. Provide our x=CHUNK_SIZE-1 slice.
            for (int i = 0; i < CHUNK_SIZE; ++i) {
                for (int j = 0; j < CHUNK_SIZE; ++j) {
                    if (!blocks[CHUNK_SIZE-1][i][j].isAir()) {
                        result.xPos[i] |= (1u << j);
                    }
                }
            }
            break;
        case 2: // We are the +Y neighbor. Provide our y=0 slice.
            for (int i = 0; i < CHUNK_SIZE; ++i) {
                for (int j = 0; j < CHUNK_SIZE; ++j) {
                    if (!blocks[i][0][j].isAir()) {
                        result.yNeg[i] |= (1u << j);
                    }
                }
            }
            break;
        case 3: // We are the -Y neighbor. Provide our y=CHUNK_SIZE-1 slice.
            for (int i = 0; i < CHUNK_SIZE; ++i) {
                for (int j = 0; j < CHUNK_SIZE; ++j) {
                    if (!blocks[i][CHUNK_SIZE-1][j].isAir()) {
                        result.yPos[i] |= (1u << j);
                    }
                }
            }
            break;
        case 4: // We are the +Z neighbor. Provide our z=0 slice.
            for (int i = 0; i < CHUNK_SIZE; ++i) {
                for (int j = 0; j < CHUNK_SIZE; ++j) {
                    if (!blocks[i][j][0].isAir()) {
                        result.zNeg[i] |= (1u << j);
                    }
                }
            }
            break;
        case 5: // We are the -Z neighbor. Provide our z=CHUNK_SIZE-1 slice.
            for (int i = 0; i < CHUNK_SIZE; ++i) {
                for (int j = 0; j < CHUNK_SIZE; ++j) {
                    if (!blocks[i][j][CHUNK_SIZE-1].isAir()) {
                        result.zPos[i] |= (1u << j);
                    }
                }
            }
            break;
    }

    return result;
}

/**
 * Apply a mesh built by a worker thread.
 * Transfers ownership of the mesh data to this chunk.
 */
void Chunk::applyMesh(ChunkMesh&& mesh)
{
    meshCPU = std::move(mesh);
}
