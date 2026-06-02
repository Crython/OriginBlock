
#ifndef WORLDS_HPP
#define WORLDS_HPP

#include "chunkHandler.hpp"
#include "terrain.hpp"
#include "shader.hpp"
#include "glm/glm/glm.hpp"
#include <glm/glm/gtc/matrix_transform.hpp>
#include "constants.hpp"
#include <unordered_set>
#include <deque>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <mutex>
#include "glfw/glfw3.h"
#include <cassert>

class ThreadedChunkSystem; // Forward declaration

struct RaycastHit {
    glm::ivec3 block;   // world block coords
    glm::ivec3 normal;  // face normal of the hit
};

struct BlockRef {
    Chunk* chunk;
    glm::ivec3 local;
};  

struct WorldLightingData {
    alignas(16) glm::vec3 sunDir = glm::vec3(0.0f);       // 0-12 (aligned to 16)
    float sunIntensity = 0.0f;                             // 12-16

    alignas(16) glm::vec3 sunColor = glm::vec3(0.0f);     // 16-28 (aligned to 32)
    float globalExposure = 0.0f;                           // 28-32

    alignas(16) glm::vec3 ambientColor = glm::vec3(0.0f); // 32-44 (aligned to 48)
    float ambientStrength = 0.0f;                          // 44-48

    alignas(16) glm::vec3 fogColor = glm::vec3(0.0f);     // 48-60 (aligned to 64)
    float fogDensity = 0.0f;                               // 60-64

    // Total: 64 bytes (std140 layout with vec3+float pairs)
};
static_assert(sizeof(WorldLightingData) == 64, "WorldLightingData must be 64 bytes for std140 UBO");

class World {
public:
    World();
    ~World();
    
    // Disable copy
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    using ChunkMap = std::unordered_map<ChunkCoord, std::shared_ptr<Chunk>, ChunkCoordHash>;
    using ChunkIter = ChunkMap::iterator;

    struct LoadingPlan {
        std::vector<ChunkCoord> neededOrdered;
        std::unordered_set<ChunkCoord, ChunkCoordHash> neededSet;
        glm::dvec3 playerPos;
        bool ready = false;
    };
    
    std::shared_ptr<LoadingPlan> activePlan;
    std::shared_ptr<LoadingPlan> nextPlan; // Plan being built
    std::mutex planMutex;
    bool planInProgressBar = false;

    ChunkMap chunks;
    
    enum class ChunkState : uint8_t { EMPTY, HIDDEN_SOLID };
    // Map of chunks that are unloaded and their state
    std::unordered_map<ChunkCoord, ChunkState, ChunkCoordHash> unloadedChunks;

    std::deque<ChunkCoord> dirtyChunks;
    uint32_t worldSeed = 0;

    glm::dvec3 lastUpdatePos = glm::dvec3(1e9); // Force update on first frame
    std::vector<Chunk*> activeMeshes; // Chunks that have a GPU mesh
    
    std::vector<ChunkCoord> neededChunksCache;
    std::unordered_set<ChunkCoord, ChunkCoordHash> neededSetCache;
    size_t nextLoadIndex = 0;
    bool radiusFullApplied = false;

    // Time-sliced unloading state
    std::vector<ChunkCoord> unloadQueue;
    size_t nextUnloadIndex = 0;
    bool isUnloading = false;

    WorldLightingData lightData;

    // Threaded chunk system
    std::unique_ptr<ThreadedChunkSystem> chunkThreads;
    
    // Enable/disable threaded chunk loading
    bool useThreadedChunks = false;
        
    void setRandSeed();

    // Debug: reload all chunks
    void reloadAllChunks(bool regenerate = false);

    // Misc
    void markDirty(Chunk* c);
    void draw(Shader& shader, const glm::vec3& lightDir, const glm::mat4& VP, const glm::dvec3& cameraPos, int& verticesRendered);
    ChunkCoord worldToChunk(glm::ivec3 worldPos) const;

    // Block interaction
    void breakBlock(const RaycastHit& hit);
    void placeBlock(const RaycastHit& hit, BlockType type);
    bool raycast(const glm::dvec3& origin, const glm::vec3& direction, float maxDist, RaycastHit& outHit);
    bool isAirWorld(int wx, int wy, int wz);
    void setBlockWorld(int wx, int wy, int wz, BlockType type);

    bool isSolidBlockWorld(const glm::ivec3& worldPos);
    bool checkCollision(const glm::dvec3& pos, const glm::vec3& dimensions);
    void resolveCollision(glm::dvec3& pos, glm::vec3& velocity, const glm::vec3& dimensions, bool& onGround);


    // Chunk management
    void updateLoadedChunks(const glm::dvec3& playerPos, glm::vec3 viewDir, const uint32_t seed); 
    void rebuildDirtyChunks(const glm::vec3& lightDir, const glm::dvec3& playerPos);
    void loadChunk(const ChunkCoord& coord, int seed, bool immediate = false);
    ChunkMap::iterator unloadChunk(ChunkMap::iterator it);
    void reviveChunk(const ChunkCoord& coord, bool immediate = false);
    Chunk* getChunk(const ChunkCoord& c);
    Chunk* getChunkFromWorldPos(const glm::ivec3& worldPos);
    // Helper to check if an unloaded chunk is Hidden Solid
    bool isChunkHiddenSolid(const ChunkCoord& c);
    
    // Threaded chunk loading support
    void finalizeChunkLoad(const ChunkCoord& coord, std::shared_ptr<Chunk> chunk);
    void finalizeChunkMesh(const ChunkCoord& coord, ChunkMesh&& mesh);
    void processThreadedWork();


private:

    BlockRef getBlockRef(glm::ivec3 worldPos);

    
    Chunk* getChunkAtWorld(const glm::ivec3& worldPos);
    inline glm::ivec3 worldToLocal(int wx, int wy, int wz);
    void markChunkAndNeighborsDirty(const ChunkCoord& chunkPos);
    std::array<Chunk*, 6> getNeighbors(Chunk* c);
};


#endif // WORLDS_HPP