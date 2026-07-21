#include "pch.h"
#include "ThreadedChunkSystem.hpp"
#include "Worlds.hpp"

ThreadedChunkSystem::ThreadedChunkSystem(size_t numThreads)
{
    if (numThreads == 0) {
        numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    }

    // Spawn worker threads
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back(&ThreadedChunkSystem::workerLoop, this);
    }
}

ThreadedChunkSystem::~ThreadedChunkSystem()
{
    // Signal all workers to stop
    stopFlag.store(true);
    genCV.notify_all();
    meshCV.notify_all();

    // Wait for all workers to finish
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadedChunkSystem::workerLoop()
{
    while (!stopFlag.load()) {
        // Try to get a mesh job first (Priority: Rendering > Generation)
        {
            std::unique_lock<std::mutex> lock(meshMutex);
            if (!meshQueue.empty()) {
                MeshJob job = std::move(meshQueue.front());
                meshQueue.pop();
                lock.unlock();

                if (!isCancelled(job.coord)) {
                    processMeshJob(job);
                }
                
                // Erase from pending set ONLY AFTER processing (or skipping)
                std::lock_guard<std::mutex> eraseLock(meshMutex);
                pendingMeshCoords.erase(job.coord);
                continue;
            }
        }

        // Try to get a generation job
        {
            std::unique_lock<std::mutex> lock(genMutex);
            if (!genQueue.empty()) {
                GenerationJob job = std::move(genQueue.front());
                genQueue.pop();
                lock.unlock();

                if (!isCancelled(job.coord)) {
                    processGenerationJob(job);
                }
                
                // Erase from pending set ONLY AFTER processing
                std::lock_guard<std::mutex> eraseLock(genMutex);
                pendingGenCoords.erase(job.coord);
                continue;
            }
        }

        // No work available, wait for notification
        {
            std::unique_lock<std::mutex> lock(genMutex);
            genCV.wait_for(lock, std::chrono::milliseconds(10), [this] {
                return stopFlag.load() || !genQueue.empty();
            });
        }
    }
}

void ThreadedChunkSystem::processGenerationJob(GenerationJob& job)
{
    // Create and generate chunk on worker thread
    auto chunk = std::make_shared<Chunk>(job.coord, nullptr, job.seed, false); // false = don't generate yet
    
    // Now generate the terrain (thread-safe, static)
    chunk->generateTerrain(job.seed);

    // Push to completed queue
    {
        std::lock_guard<std::mutex> lock(completedGenMutex);
        completedGen.push(CompletedGeneration{job.coord, std::move(chunk)});
    }
}

void ThreadedChunkSystem::processMeshJob(MeshJob& job)
{
    // Build mesh data using the snapshot of neighbor info
    ChunkMesh mesh;
    
    // Use the chunk pointer captured when the job was queued
    Chunk* chunk = job.chunk.get();
    
    if (!chunk || isCancelled(job.coord)) {
        return; // Chunk was unloaded, skip
    }

    // Rebuild padding using neighbor snapshot
    chunk->rebuildPaddingFromSnapshot(
        job.neighborPadding,
        job.neighborExists,
        job.neighborHiddenSolid
    );
    // chunk->rebuildNeighbourLODsFromSnapshot(job.neighborExists, job.neighbourLODs);

    // Build the mesh (writes to mesh parameter, not chunk's meshCPU)
    // Distance was captured at queue time — no main-thread access needed
    chunk->buildGreedyMeshThreadSafe(job.lightDir, mesh, job.LOD);

    if (isCancelled(job.coord)) {
        return; // Chunk was unloaded during meshing
    }

    // Push to completed queue
    {
        std::lock_guard<std::mutex> lock(completedMeshMutex);
        completedMesh.push(CompletedMesh{job.coord, std::move(mesh)});
    }
}

void ThreadedChunkSystem::queueGeneration(const ChunkCoord& coord, int seed, World* world)
{
    // If it was cancelled, un-cancel it now since we want to load it again
    removeCancelledCoord(coord);

    std::lock_guard<std::mutex> lock(genMutex);
    
    // Don't queue if already pending
    if (pendingGenCoords.contains(coord)) {
        return;
    }

    pendingGenCoords.insert(coord);
    genQueue.push(GenerationJob{coord, seed, world});
    genCV.notify_one();
}

void ThreadedChunkSystem::queueMeshBuild(const ChunkCoord& coord, std::shared_ptr<Chunk> chunk, const glm::vec3& lightDir, World* world, const glm::dvec3& playerPos)
{
    if (!chunk) return;

    // If it was cancelled, un-cancel it now
    removeCancelledCoord(coord);

    // Capture all data the worker needs as a snapshot — no main-thread access during processing
    MeshJob job;
    job.coord = coord;
    job.lightDir = lightDir;
    job.chunk = chunk;

    // Compute and store the LOD of this chunk
    job.LOD = chunk->getLODlevel(chunk->getDistanceFromPlayerSquared(playerPos, coord));

    // Get neighbor padding data snapshot
    static const ChunkCoord offsets[6] = {
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1}
    };

    std::array<int, 6> tempLODs;
    // Loop through all neighbours this chunk has
    for (int i = 0; i < 6; ++i) {
        ChunkCoord neighborCoord = {
            coord.x + offsets[i].x,
            coord.y + offsets[i].y,
            coord.z + offsets[i].z
        };
        
        Chunk* neighbor = world->getChunk(neighborCoord);
        job.neighborExists[i] = (neighbor != nullptr);
        job.neighborHiddenSolid[i] = (!neighbor && world->isChunkHiddenSolid(neighborCoord));
        
        if (neighbor) {
            job.neighborPadding[i] = neighbor->getPaddingDataForNeighbor(i);
            tempLODs[i] = chunk->getLODlevel(chunk->getDistanceFromPlayerSquared(playerPos, neighborCoord));
        } else {
            // Zero-initialize if no neighbor
            std::memset(&job.neighborPadding[i], 0, sizeof(PaddingMasks));
        }
    }

    
    chunk->LODs.setNeighbourLODs(
        tempLODs[0],
        tempLODs[1],
        tempLODs[2],
        tempLODs[3],
        tempLODs[4],
        tempLODs[5]
    );
    

    job.neighbourLODs = chunk->LODs;

    std::lock_guard<std::mutex> lock(meshMutex);
    
    // Don't queue if already pending
    if (pendingMeshCoords.contains(coord)) {
        return;
    }

    pendingMeshCoords.insert(coord);
    meshQueue.push(std::move(job));
    meshCV.notify_one();
}

size_t ThreadedChunkSystem::processCompletedGenerations(World* world, size_t maxCount)
{
    size_t processed = 0;

    while (processed < maxCount) {
        CompletedGeneration completed;
        
        {
            std::lock_guard<std::mutex> lock(completedGenMutex);
            if (completedGen.empty()) break;
            completed = std::move(completedGen.front());
            completedGen.pop();
        }

        // Check if cancelled
        if (isCancelled(completed.coord)) {
            removeCancelledCoord(completed.coord);
            continue;
        }

        // Finalize chunk on main thread
        if (completed.chunk) {
            completed.chunk->setOwner(world);
            world->finalizeChunkLoad(completed.coord, std::move(completed.chunk));
        }

        ++processed;
    }

    return processed;
}

size_t ThreadedChunkSystem::processCompletedMeshes(World* world, size_t maxCount)
{
    size_t processed = 0;

    while (processed < maxCount) {
        CompletedMesh completed;
        
        {
            std::lock_guard<std::mutex> lock(completedMeshMutex);
            if (completedMesh.empty()) break;
            completed = std::move(completedMesh.front());
            completedMesh.pop();
        }

        // Check if cancelled
        if (isCancelled(completed.coord)) {
            removeCancelledCoord(completed.coord);
            continue;
        }

        // Finalize mesh on main thread (handles upload and auto-unload)
        world->finalizeChunkMesh(completed.coord, std::move(completed.mesh));

        ++processed;
    }

    return processed;
}

bool ThreadedChunkSystem::isPendingGeneration(const ChunkCoord& coord) const
{
    std::lock_guard<std::mutex> lock(genMutex);
    return pendingGenCoords.contains(coord);
}

bool ThreadedChunkSystem::isPendingMesh(const ChunkCoord& coord) const
{
    std::lock_guard<std::mutex> lock(meshMutex);
    return pendingMeshCoords.contains(coord);
}

void ThreadedChunkSystem::cancelJobs(const ChunkCoord& coord)
{
    std::lock_guard<std::mutex> lock(cancelledMutex);
    cancelledCoords.insert(coord);
}

bool ThreadedChunkSystem::isCancelled(const ChunkCoord& coord)
{
    std::lock_guard<std::mutex> lock(cancelledMutex);
    return cancelledCoords.contains(coord);
}

void ThreadedChunkSystem::removeCancelledCoord(const ChunkCoord& coord)
{
    std::lock_guard<std::mutex> lock(cancelledMutex);
    cancelledCoords.erase(coord);
}

size_t ThreadedChunkSystem::getGenerationQueueSize() const
{
    std::lock_guard<std::mutex> lock(genMutex);
    return genQueue.size();
}

size_t ThreadedChunkSystem::getMeshQueueSize() const
{
    std::lock_guard<std::mutex> lock(meshMutex);
    return meshQueue.size();
}
