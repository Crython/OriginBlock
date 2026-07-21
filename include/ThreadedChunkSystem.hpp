#ifndef THREADED_CHUNK_SYSTEM_HPP
#define THREADED_CHUNK_SYSTEM_HPP

#include "chunkHandler.hpp"

class World;

// Job types for the thread pool
struct GenerationJob {
    ChunkCoord coord;
    int seed;
    World* world;
};

struct MeshJob {
    ChunkCoord coord;
    glm::vec3 lightDir;
    std::shared_ptr<Chunk> chunk; // Keep the chunk alive
    int LOD;          // Distance from player, captured at queue time
    // Snapshot of neighbor solid data for thread-safe padding
    NeighbourLODs neighbourLODs;
    std::array<PaddingMasks, 6> neighborPadding;
    std::array<bool, 6> neighborExists;
    std::array<bool, 6> neighborHiddenSolid;
};

// Results from worker threads
struct CompletedGeneration {
    ChunkCoord coord;
    std::shared_ptr<Chunk> chunk;
};

struct CompletedMesh {
    ChunkCoord coord;
    ChunkMesh mesh;
};

class ThreadedChunkSystem {
public:
    ThreadedChunkSystem(size_t numThreads = 0); // 0 = auto-detect
    ~ThreadedChunkSystem();

    // Prevent copying
    ThreadedChunkSystem(const ThreadedChunkSystem&) = delete;
    ThreadedChunkSystem& operator=(const ThreadedChunkSystem&) = delete;

    // Queue a chunk for background terrain generation
    void queueGeneration(const ChunkCoord& coord, int seed, World* world);

    // Queue a chunk for background mesh building
    // playerPos is captured now (main thread) to compute LOD distance for the worker
    void queueMeshBuild(const ChunkCoord& coord, std::shared_ptr<Chunk> chunk, const glm::vec3& lightDir, World* world, const glm::dvec3& playerPos);

    // Main-thread: process completed generation work
    // Returns number of chunks processed
    size_t processCompletedGenerations(World* world, size_t maxCount);

    // Main-thread: process completed mesh work (GPU uploads)
    // Returns number of meshes uploaded
    size_t processCompletedMeshes(World* world, size_t maxCount);

    // Check if coord is pending generation
    bool isPendingGeneration(const ChunkCoord& coord) const;

    // Check if coord is pending mesh build
    bool isPendingMesh(const ChunkCoord& coord) const;

    // Cancel pending jobs for a coord (when chunk is unloaded)
    void cancelJobs(const ChunkCoord& coord);

    // Get queue sizes for debugging
    size_t getGenerationQueueSize() const;
    size_t getMeshQueueSize() const;

private:
    std::vector<std::thread> workers;
    std::atomic<bool> stopFlag{false};

    // Generation queue
    mutable std::mutex genMutex;
    std::condition_variable genCV;
    std::queue<GenerationJob> genQueue;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingGenCoords;

    // Mesh queue
    mutable std::mutex meshMutex;
    std::condition_variable meshCV;
    std::queue<MeshJob> meshQueue;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingMeshCoords;

    // Completed work (consumed by main thread)
    mutable std::mutex completedGenMutex;
    std::queue<CompletedGeneration> completedGen;

    mutable std::mutex completedMeshMutex;
    std::queue<CompletedMesh> completedMesh;

    // Cancelled coords (to skip completed work for unloaded chunks)
    mutable std::mutex cancelledMutex;
    std::unordered_set<ChunkCoord, ChunkCoordHash> cancelledCoords;

    void workerLoop();
    void processGenerationJob(GenerationJob& job);
    void processMeshJob(MeshJob& job);

    bool isCancelled(const ChunkCoord& coord);
    void removeCancelledCoord(const ChunkCoord& coord);
};

#endif // THREADED_CHUNK_SYSTEM_HPP
