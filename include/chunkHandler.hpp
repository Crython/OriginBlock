
#ifndef CHUNK_HANDLER_HPP
#define CHUNK_HANDLER_HPP

#include "types.hpp"
#include "constants.hpp"
#include <vector>
#include <array>
#include "textureManager.hpp"
#include <iostream>
#include "glfw/glfw3.h"
#include <bit>
#include "helpers.hpp"
#include <string>

using FaceMask = uint16_t[CHUNK_SIZE]; // 16 rows x 16 bits

struct NeighbourLODs
{
    uint32_t LODs; // 3-bits per neighbour LOD
    uint8_t isInvalid;

    std::array<int, 6> getNeighbourLODs() const {
        int n1 = 0, n2 = 0, n3 = 0, n4 = 0, n5 = 0, n6 = 0;

        // Get the LODs
        n1 = LODs & 0x7;
        n2 = (LODs >> 3) & 0x7;
        n3 = (LODs >> 6) & 0x7;
        n4 = (LODs >> 9) & 0x7;
        n5 = (LODs >> 12) & 0x7;    
        n6 = (LODs >> 15) & 0x7;


        // Find out if the LODs are valid
        bool n1v =  isInvalid & 0x1;
        bool n2v = (isInvalid & 0x2)  >> 1;
        bool n3v = (isInvalid & 0x4)  >> 2;
        bool n4v = (isInvalid & 0x8)  >> 3;
        bool n5v = (isInvalid & 0x10) >> 4;
        bool n6v = (isInvalid & 0x20) >> 5;

        // If invalid, return -1
        if (n1v) n1 = -1;
        if (n2v) n2 = -1;
        if (n3v) n3 = -1;
        if (n4v) n4 = -1;
        if (n5v) n5 = -1;
        if (n6v) n6 = -1;

        return { n1, n2, n3, n4, n5, n6 };
    }
    void setNeighbourLODs(int n1, int n2, int n3, int n4, int n5, int n6) {

        LODs &= ~((0x7 << 0) | (0x7 << 3) | (0x7 << 6) | (0x7 << 9) | (0x7 << 12) | (0x7 << 15));
        LODs |= (n1 & 0x7) << 0;
        LODs |= (n2 & 0x7) << 3;
        LODs |= (n3 & 0x7) << 6;
        LODs |= (n4 & 0x7) << 9;
        LODs |= (n5 & 0x7) << 12;
        LODs |= (n6 & 0x7) << 15;


        // If the LOD is negative, it is not valid
        isInvalid &= ~(0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20);
        if (n1 < 0) isInvalid |= 0x1;
        if (n2 < 0) isInvalid |= 0x2;
        if (n3 < 0) isInvalid |= 0x4;
        if (n4 < 0) isInvalid |= 0x8;
        if (n5 < 0) isInvalid |= 0x10;
        if (n6 < 0) isInvalid |= 0x20;

    }

    // Set the neigbour's LOD based on a 0-indexed address
    void setNeighbourLOD(int val, int idx) {
        // Clear previous 3 bits for this neighbor
        LODs &= ~(0x7 << (idx * 3));
        LODs |= (val & 0x7) << (idx * 3);

        // Clear previous validity bit for this neighbor
        isInvalid &= ~(0x1 << idx);
        if (val < 0) isInvalid |= (0x1 << idx);

    }
};

struct PaddingMasks
{
    FaceMask xNeg, xPos;
    FaceMask yNeg, yPos;
    FaceMask zNeg, zPos;
};

// Flags for marking chunk dirty states
enum DirtyFlags : uint8_t {
        Dirty_None = 0,
        Dirty_Mesh = 1 << 0,
        Dirty_Padded = 1 << 1
};

struct ChunkCoord {
    int x, y, z;

    ChunkCoord() : x(0), y(0), z(0) { }
    ChunkCoord(int x_, int y_, int z_) : x(x_), y(y_), z(z_) { }

    bool operator==(const ChunkCoord& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
    ChunkCoord operator+(const ChunkCoord& other) const {
        return ChunkCoord(x + other.x, y + other.y, z + other.z);
    }
    glm::ivec3 operator+(const glm::ivec3 other) const {
        return glm::ivec3(x + other.x, y + other.y, z + other.z);
    }

};
struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& c) const {
        std::size_t h = 0;
        h ^= std::hash<int>{}(c.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
struct ChunkCornerHeights {
    uint16_t h00, h01, h10, h11;
};


struct _Block {
    // uint8_t lightLevel; // 0-31 -> 5
    // uint8_t type; // 0-127 -> 7
    // uint8_t metadata; // 0-15 -> 4
    // Total 16 bits -> uint16_t; 2 bytes per block
    uint16_t all; // layout: llllltttttttmmmm

    _Block() : all(0) {} // default: type: 0, metadata: 0, light: 0

    inline uint8_t getType()     const { return (all >> 4) & 0x1F; }
    inline uint8_t getMetadata() const { return all & 0xF; }
    inline uint8_t getLight()    const { return (all >> 11) & 0x7F; }

    inline void setType(const uint8_t type) { all = (all & 0xF) | ((type & 0x1F) << 4); }
    inline void setMetadata(const uint8_t metadata) { all = (all & 0xFFF0) | (metadata & 0xF); }
    inline void setLight(const uint8_t light) { all = (all & 0x7FF) | ((light & 0x7F) << 11); }
    inline void setValues(const uint8_t type, const uint8_t metadata, const uint8_t light) { all = ((light & 0x7F) << 11) | ((type & 0x1F) << 4) | (metadata & 0xF); }

    inline bool isAir() const { return getType() == 0; }

};


class ChunkMesh {
public:
    ChunkMesh() = default;
    ~ChunkMesh() {
        clear();
    }
    
    // Move constructor and assignment for thread-safe transfer
    ChunkMesh(ChunkMesh&& other) noexcept
        : vertices(std::move(other.vertices))
        , indices(std::move(other.indices)) {}
    
    ChunkMesh& operator=(ChunkMesh&& other) noexcept {
        if (this != &other) {
            vertices = std::move(other.vertices);
            indices = std::move(other.indices);
        }
        return *this;
    }
    
    // Disable copy (meshes should be moved, not copied)
    ChunkMesh(const ChunkMesh&) = delete;
    ChunkMesh& operator=(const ChunkMesh&) = delete;

	// Vertices and Indices per face direction (6 directions)
    std::array<std::vector<Vertex>, 6> vertices;
    std::array<std::vector<uint16_t>, 6> indices;

    void clear() {
        for (int i = 0; i < 6; i++) {
            vertices[i].clear();
            indices[i].clear();
        }
    }

    size_t totalVertexCount() const {
        size_t total = 0;
        for (int i = 0; i < 6; i++) total += vertices[i].size();
        return total;
	}

    size_t totalIndexCount() const {
        size_t total = 0;
        for (int i = 0; i < 6; i++) total += indices[i].size();
        return total;
    }

    bool empty() const {
        for (const auto& v : vertices) if (!v.empty()) return false;
        return true;
    }

};


class ChunkMeshGPU {
public:
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ebo = 0;
    size_t indexCount = 0;

    struct FaceRange {
        GLint first;  // Offset in the index buffer
        GLsizei count; // Number of indices
    };
    FaceRange faceRanges[6];

    ~ChunkMeshGPU() {
        destroy();
    }

    void destroy()
    {
        if (vao)
        {
            glDeleteVertexArrays(1, &vao);
            glDeleteBuffers(1, &vbo);
            glDeleteBuffers(1, &ebo);
        }

        vao = vbo = ebo = 0;
        indexCount = 0;
    }

    void upload(const ChunkMesh& mesh)
    {
        size_t totalVerts = mesh.totalVertexCount();
        size_t totalIndices = mesh.totalIndexCount();

        if (totalVerts == 0 || totalIndices == 0) {
            destroy();
            return;
        }

        if (vao == 0) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glGenBuffers(1, &ebo);
        }

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, totalVerts * sizeof(Vertex), nullptr, GL_STATIC_DRAW);

        // Upload vertex data face by face
        size_t vOffset = 0;
        for (int i = 0; i < 6; i++) {
            if (!mesh.vertices[i].empty()) {
                glBufferSubData(GL_ARRAY_BUFFER, vOffset * sizeof(Vertex), mesh.vertices[i].size() * sizeof(Vertex), mesh.vertices[i].data());
                vOffset += mesh.vertices[i].size();
            }
        }

        // Upload index data face by face and track ranges
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, totalIndices * sizeof(uint16_t), nullptr, GL_STATIC_DRAW);

        size_t iOffset = 0;
        for (int i = 0; i < 6; i++) {
            if (!mesh.indices[i].empty()) {
                glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, iOffset * sizeof(uint16_t), mesh.indices[i].size() * sizeof(uint16_t), mesh.indices[i].data());
                faceRanges[i] = { (GLint)(iOffset * sizeof(uint16_t)), (GLsizei)mesh.indices[i].size() };
                iOffset += mesh.indices[i].size();
            } else {
                faceRanges[i] = { 0, 0 };
            }
        }

        // Attr setup (same as before)
        glEnableVertexAttribArray(0);
        glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(Vertex), (void*)offsetof(Vertex, data1));
        glEnableVertexAttribArray(1);
        glVertexAttribIPointer(1, 1, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, data2));

        glBindVertexArray(0);
        indexCount = totalIndices;
    }

    void draw(int& totalTris, const glm::vec3& relPos, bool useDirCulling = false) const
    {
        if (vao == 0 || indexCount == 0) return;

        glBindVertexArray(vao);
        
        if (!useDirCulling) {
            glDrawElements(GL_TRIANGLES, (GLsizei)indexCount, GL_UNSIGNED_SHORT, nullptr);
            totalTris += (int)indexCount / 3;
        } else {
            // SAFE AABB-BASED DIRECTIONAL CULLING
            // Culls faces that are guaranteed to be back-facing based on camera position relative to the chunk.
            // relPos is (ChunkWorldPos - CameraWorldPos)
            
            // FACES indices: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
            bool cullFace[6] = { false };

            // If the chunk is entirely to the right (X > 0), we see its -X side. +X faces are hidden.
            if (relPos.x > 0) cullFace[0] = true; // Cull +X
            // If the chunk is entirely to the left (X + size < 0), we see its +X side. -X faces are hidden.
            if (relPos.x + CHUNK_SIZE < 0) cullFace[1] = true; // Cull -X

            if (relPos.y > 0) cullFace[2] = true; // Cull +Y
            if (relPos.y + CHUNK_SIZE < 0) cullFace[3] = true; // Cull -Y

            if (relPos.z > 0) cullFace[4] = true; // Cull +Z
            if (relPos.z + CHUNK_SIZE < 0) cullFace[5] = true; // Cull -Z

            for (int i = 0; i < 6; i++) {
                if (faceRanges[i].count > 0 && !cullFace[i]) {
                    glDrawElements(GL_TRIANGLES, faceRanges[i].count, GL_UNSIGNED_SHORT, (void*)(size_t)faceRanges[i].first);
                    totalTris += faceRanges[i].count / 3;
                }
            }
        }
        glBindVertexArray(0);
    }


};


class World; // Forward declaration
class Terrain; // Forward declaration for threading

class Chunk {
public:
    // Original constructor (generates immediately)
    Chunk(const ChunkCoord& coord, World* world, int seed);
    
    // Constructor for threaded generation (deferred generation if generate=false)
    Chunk(const ChunkCoord& coord, World* world, int seed, bool generate);
    
    ~Chunk();
    World* owner = nullptr;

    void clearMesh();

    // Original methods (for main thread or single-threaded mode)
    void rebuildPadding();
    void rebuildNeighbourLODs();
    void buildGreedyMesh(const glm::vec3& lightDir, const int LOD_SquaredDistance);
    void uploadMesh();
    void draw(int& totalTris, const glm::vec3& viewDir);

    // Thread-safe generation (doesn't access World)
    void generateTerrain(int seed);
    
    // Thread-safe padding rebuild using pre-captured neighbor data
    void rebuildPaddingFromSnapshot(
        const std::array<PaddingMasks, 6>& neighborPadding,
        const std::array<bool, 6>& neighborExists,
        const std::array<bool, 6>& neighborHiddenSolid
    );
    void rebuildNeighbourLODsFromSnapshot(const std::array<int, 6>& neighbourLODs, const std::array<bool, 6>& neighborExists);

    // Thread-safe mesh building (outputs to provided mesh, not meshCPU)
    void buildGreedyMeshThreadSafe(const glm::vec3& lightDir, ChunkMesh& outMesh, const int LOD_SquaredDistance);
    
    // Get padding data for a specific neighbor direction
    PaddingMasks getPaddingDataForNeighbor(int neighborIndex) const;
    
    // Apply mesh from worker thread
    void applyMesh(ChunkMesh&& mesh);
    
    // Set owner after construction (for threaded creation)
    void setOwner(World* world) { owner = world; }

    void setBlock(const glm::ivec3& localPos, BlockType type, uint8_t metadata = 0);
    bool isAir(const glm::ivec3 local);
    int getSolidBlockCount() const { return solidBlockCount; }
    bool isAirOnly() const { return solidBlockCount == 0; }
    bool hasMesh() const;

    uint8_t getDirtyFlags() const;
    bool isDirty(DirtyFlags df) const;
    void markDirty(DirtyFlags df);
    void clearDirty(DirtyFlags df);
    ChunkCoord getChunkPos() const { return ChunkCoord(chunkX, chunkY, chunkZ); }

    ChunkMesh meshCPU;
    int solidBlockCount = 0;
    
    // Optimization flag
    bool isInDirtyList = false;
    bool meshDirtyDuringBuild = false;
    
    // Access blocks for neighbor padding calculation
    const _Block& getBlock(int x, int y, int z) const { return blocks[x][y][z]; }

    NeighbourLODs LODs;
    int chunkLOD = -1; // default of -1 to mark the "not set"

private:
    void generate(int seed);
    void setPosition(ChunkCoord c);
    void countSolidBlocks();
    inline bool isSolidSafe(const Chunk* c, int x, int y, int z);
    inline bool neighborSolid(const PaddingMasks& pad, int axis, int dir, int x, int y, int z);
    
    // Internal mesh building helper (used by both buildGreedyMesh and buildGreedyMeshThreadSafe)
    void buildGreedyMeshInternal(const glm::vec3& lightDir, ChunkMesh& outMesh, const int LOD_SquaredDistance);

private:
    
    int chunkX{}, chunkY{}, chunkZ{};
    
    // Efficient padding for neighboring chunks
    PaddingMasks padding;
    // Array to store the blocks of the chunks
    _Block blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];

    uint8_t dirtyFlags = Dirty_None;

    // Meshes to draw
    ChunkMeshGPU meshGPU;
};


#endif // CHUNK_HANDLER_HPP
