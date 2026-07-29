/*
 * WORLDS.CPP
 * 
 * Manages the world state, including chunk loading/unloading, rendering, and physics.
 * 
 * CHUNK LOADING SYSTEM:
 * - Async plan-based loading: Background thread computes ideal chunk set based on player position
 * - Time-sliced operations: Loading and unloading spread across frames to prevent frame drops
 * - Threaded chunk generation: Worker threads handle terrain generation and mesh building
 * - Frustum culling: Only visible chunks are rendered each frame
 * 
 * LOADING PHASES (in updateLoadedChunks):
 * 1. Plan generation: Async thread builds sorted list of needed chunks
 * 2. Chunk loading: Time-sliced creation of new chunks
 * 3. Chunk unloading: Iterator-based removal of distant chunks
 * 4. Registry pruning: Periodic cleaning of unloaded chunk metadata
 */
#include "pch.h"
#include "Worlds.hpp"
#include "ThreadedChunkSystem.hpp"
#include "terrain.hpp"

// ===========================
// CHUNK LOADING CONSTANTS
// ===========================

// Frame budgets to prevent stuttering
constexpr int UNLOAD_CHECKS_PER_FRAME = 100;     // Max chunks to check for unloading per frame
constexpr int PRUNE_INTERVAL_FRAMES = 120;       // Frames between registry pruning (every 2 seconds at 60fps)

// Movement thresholds
constexpr double REPLAN_DISTANCE_SQ = 16.0;      // Player must move 4+ blocks to trigger replan

// Chunk priority scoring
constexpr float VIEW_BIAS_WEIGHT = 128.0f;       // Prioritize chunks in view direction
constexpr float GROUND_BIAS_WEIGHT = 16.0f;      // Prioritize chunks below player

// World constructor
World::World()
{
    lightData.sunDir = glm::normalize(glm::vec3(0.5f, 1.0f, 0.5f));
    lightData.sunIntensity = 2.3f;
    lightData.sunColor = glm::vec3(0.99f, 0.77f, 0.46f); // Light salmon white
    lightData.globalExposure = 3.6f;
    lightData.ambientColor = glm::vec3(0.39f, 0.65f, 0.66f); // Slate blue 
    lightData.ambientStrength = 1.8f;
    lightData.fogColor = glm::vec3(0.67, 0.73, 0.63); // Fog greyish blue
    lightData.fogDensity = 0.001f;

    // Initialize threaded chunk system
    chunkThreads = std::make_unique<ThreadedChunkSystem>();
}

World::~World()
{
    // Destroy thread system first to stop all workers
    chunkThreads.reset();
    
    // Then clear chunks
    chunks.clear();
}


// ===========================
// FRUSTUM CULLING UTILITIES
// ===========================

/**
 * Extract frustum planes from a view-projection matrix.
 * Returns 6 planes (left, right, bottom, top, near, far) in normalized form.
 */
Frustum extractFrustum(const glm::mat4& VP) {
    Frustum frustum;

    // Left plane
    frustum.planes[0] = glm::vec4(
        VP[0][3] + VP[0][0],
        VP[1][3] + VP[1][0],
        VP[2][3] + VP[2][0],
        VP[3][3] + VP[3][0]
    );

    // Right plane
    frustum.planes[1] = glm::vec4(
        VP[0][3] - VP[0][0],
        VP[1][3] - VP[1][0],
        VP[2][3] - VP[2][0],
        VP[3][3] - VP[3][0]
    );

    // Bottom plane
    frustum.planes[2] = glm::vec4(
        VP[0][3] + VP[0][1],
        VP[1][3] + VP[1][1],
        VP[2][3] + VP[2][1],
        VP[3][3] + VP[3][1]
    );

    // Top plane
    frustum.planes[3] = glm::vec4(
        VP[0][3] - VP[0][1],
        VP[1][3] - VP[1][1],
        VP[2][3] - VP[2][1],
        VP[3][3] - VP[3][1]
    );

    // Near plane
    frustum.planes[4] = glm::vec4(
        VP[0][3] + VP[0][2],
        VP[1][3] + VP[1][2],
        VP[2][3] + VP[2][2],
        VP[3][3] + VP[3][2]
    );

    // Far plane
    frustum.planes[5] = glm::vec4(
        VP[0][3] - VP[0][2],
        VP[1][3] - VP[1][2],
        VP[2][3] - VP[2][2],
        VP[3][3] - VP[3][2]
    );

    // Normalize planes
    for (int i = 0; i < 6; i++) {
        float length = glm::length(glm::vec3(frustum.planes[i]));
        frustum.planes[i] /= length;
    }

    return frustum;
}
inline bool aabbInFrustum(const Frustum& f, const glm::vec3& min, const glm::vec3& max)
{
    for (int i = 0; i < 6; ++i)
    {
        const glm::vec3 normal = glm::vec3(f.planes[i]);

        glm::vec3 p = {
            (normal.x >= 0) ? max.x : min.x,
            (normal.y >= 0) ? max.y : min.y,
            (normal.z >= 0) ? max.z : min.z
        };

        if (glm::dot(normal, p) + f.planes[i].w < 0)
            return false; // fully outside
    }
    return true;
}
/**
 * Test if a sphere intersects the frustum.
 * Returns true if partially or fully inside, false if completely outside.
 */
inline bool sphereInFrustum(const Frustum& frustum, const glm::vec3& center, float radius) {
    for (int i = 0; i < 6; i++) {
        const auto& plane = frustum.planes[i];
        // Plane equation: Ax + By + Cz + D = 0
        // If distance from sphere center to plane < -radius, sphere is outside
        float distance = glm::dot(glm::vec3(plane.x, plane.y, plane.z), center) + plane.w;
        if (distance < -radius) return false; // fully outside
    }
    return true; // partially or fully inside
}

inline int floorDiv(int a, int b) {
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

// ===========================
// WORLD STATE MANAGEMENT
// ===========================

ChunkCoord World::worldToChunk(glm::ivec3 worldPos) const {
    return {
        floorDiv(worldPos.x, CHUNK_SIZE),
        floorDiv(worldPos.y, CHUNK_SIZE),
        floorDiv(worldPos.z, CHUNK_SIZE)
    };
}

void World::reloadAllChunks(bool regenerate)
{

	// Clear dirty and unloaded lists
    dirtyChunks.clear();
	unloadedChunks.clear();

    if (regenerate) {
        chunks.clear(); // Clear chunks to force generation (performs a reset on the world)
        ColumnCache::clearCache();
    }

	Terrain::clearCounters(); // Reset static terrain generation counters
    isUnloading = false;
    radiusFullApplied = false;
    activePlan.reset();
    nextPlan.reset();
    planInProgressBar = false;
    lastUpdatePos = glm::dvec3(1e12);

    activeMeshes.clear(); // Clear optimized list
    for (auto& [coord, chunk] : chunks)
    {
        Chunk* c = chunk.get();
        c->isInDirtyList = false; // Reset flags
        markDirty(c);
        
        if (!regenerate && c->hasMesh()) {
            activeMeshes.push_back(c);
        }
    }
}

/**
 * Mark a chunk as dirty (needs mesh rebuild).
 * Uses O(1) deduplication via isInDirtyList flag.
 */
void World::markDirty(Chunk* c)
{
    if (!c) return;

    if (chunkThreads && chunkThreads->isPendingMesh(c->getChunkPos())) {
        c->meshDirtyDuringBuild = true;
    }

    if (!c->isDirty(Dirty_Mesh)) {
        c->markDirty(Dirty_Mesh);
    }
    
    // O(1) deduplication check
    if (!c->isInDirtyList) {
        c->isInDirtyList = true;
        dirtyChunks.push_back(c->getChunkPos());
    }
}

void World::rebuildDirtyChunks(const glm::vec3& lightDir, const glm::dvec3& playerPos) {
    int count = 0;
    int totalPops = 0;
    
    constexpr int UPDATE_BUDGET = MAX_CHUNK_UPDATES_PER_FRAME;
    constexpr int MAX_POPS = MAX_CHUNKS_POPPED_PER_FRAME; // Safety limit to prevent stalling on stale data
    
    // Process up to update budget dirty chunks
    while (!dirtyChunks.empty() && count < UPDATE_BUDGET && totalPops < MAX_POPS) {
        totalPops++;
        ChunkCoord cc = dirtyChunks.front();
        dirtyChunks.pop_front();

        auto mapIt = chunks.find(cc);
        if (mapIt == chunks.end()) continue;
        std::shared_ptr<Chunk> chunkPtr = mapIt->second;
        Chunk* c = chunkPtr.get();

        if (!c) continue; // Should not happen with shared_ptr map, but safe check
        
        c->isInDirtyList = false; // Reset flag so it can be re-added

        if (useThreadedChunks && chunkThreads) {
            // Queue for threaded meshing
            chunkThreads->queueMeshBuild(cc, chunkPtr, lightDir, this, playerPos);
        } else {
            // Synchronous fallback
            glm::dvec3 chunkPosVec = {cc.x, cc.y, cc.z};
            glm::dvec3 diff = playerPos / (double)CHUNK_SIZE - chunkPosVec; // Subtract the chunk space player pos from the chunk pos
            int LOD = c->getLODlevel((int)(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z));
            
            c->rebuildPadding();
            c->rebuildNeighbourLODs(playerPos);
            c->chunkLOD = LOD;
            c->buildGreedyMesh(lightDir, LOD);


            // Safe auto-unload for EMPTY AIR chunks
            if (c->solidBlockCount == 0 && !unloadedChunks.contains(cc))
            {
                unloadChunk(mapIt);
                unloadedChunks[cc] = ChunkState::EMPTY;
                continue;
            }

            c->clearDirty(Dirty_Mesh);
            bool wasMeshed = c->hasMesh();
            c->uploadMesh();
            
            if (!wasMeshed && c->hasMesh()) {
                activeMeshes.push_back(c);
            } else if (wasMeshed && !c->hasMesh()) {
                activeMeshes.erase(std::remove(activeMeshes.begin(), activeMeshes.end(), c), activeMeshes.end());
            }
        }
        count++;
    }
}

// ===========================
// RENDERING
// ===========================

/**
 * Draw all chunks with frustum culling.
 * Uses sphere-based frustum tests and relative positioning for precision.
 */
void World::draw(Shader& shader, const glm::vec3& lightDir, const glm::mat4& VP, const glm::dvec3& cameraPos, int& verticesRendered)
{        
	// Precompute chunk bounding sphere radius
    const float chunkRadius = glm::length(glm::vec3(CHUNK_SIZE / 2.0f)) * 1.1f;  // ~9.24, slight padding

    // Get camera position as double precision
    glm::dvec3 camPosD = cameraPos;

    // We'll use a float approximation of camera position for GPU
    glm::vec3 camPosF = glm::vec3(camPosD);  // safe truncation to float

    // Optional: extract view matrix from shader if you want to keep it simple
    // But ideally you pass the view matrix already centered at origin

    // Bind shader
    shader.bind();

    // Set uniforms that don't change per chunk
    //shader.setVec3("ulightDir", lightDir);  // if not using UBO
    shader.setMat4("uViewProj", VP);

    // Frustum culling (your existing one)
    Frustum frustum = extractFrustum(VP);

    float total = 0;
    float passed = 0;
	int totalTris = 0;

    //std::cout << "Drawing " << chunks.size() << " chunks\n";
    // Iterate only over chunks that have meshes
    for (Chunk* chunk : activeMeshes) {
        
        // Compute relative position (double precision subtract)
        glm::dvec3 chunkWorld(
            chunk->getChunkPos().x * CHUNK_SIZE,
            chunk->getChunkPos().y * CHUNK_SIZE,
            chunk->getChunkPos().z * CHUNK_SIZE
        );

        glm::vec3 relPos = glm::vec3(chunkWorld - cameraPos);

        // Frustum cull using relative AABB BEFORE updating shader uniforms
        glm::vec3 chunkCenterRel = relPos + glm::vec3(CHUNK_SIZE / 2.0f);  // relative center

        total++;
        if (!sphereInFrustum(frustum, chunkCenterRel, chunkRadius)) continue;
        passed++;

        // Shift model only for the surviving chunks
        glm::mat4 model = glm::translate(glm::mat4(1.0f), relPos);
        shader.setMat4("uModel", model);

        // Draw the chunk using relative position for optimized culling
        chunk->draw(totalTris, relPos);
    } 
	//std::cout << "Total Chunks: " << total << ", Rendered: " << passed << ", Tris: " << totalTris << "\n";
    verticesRendered = totalTris * 2; // Each quad = 2 tris, 4 verts => vertices = tris * 2
}

// ===========================
// BLOCK INTERACTION
// ===========================

void World::breakBlock(const RaycastHit& hit)
{
    BlockRef ref = getBlockRef(hit.block);
    if (!ref.chunk) return;

	// World coords of the block
	int wx = hit.block.x;
	int wy = hit.block.y;
	int wz = hit.block.z;


    setBlockWorld(wx, wy, wz, BlockType::AIR);

    markChunkAndNeighborsDirty(worldToChunk(hit.block));
}

void World::placeBlock(const RaycastHit& hit, BlockType type)
{    
    
    glm::ivec3 placePos = hit.block + hit.normal;  // place on the face hit

    ChunkCoord cc = worldToChunk(placePos);
    Chunk* c = getChunk(cc);

    if (!c) {
        if (unloadedChunks.contains(cc)) {
            reviveChunk(cc, true); // Synchronous revival
            c = getChunk(cc);
        }
    }
    if (!c) { return; }

    glm::ivec3 local = glm::ivec3(
        placePos.x - cc.x * CHUNK_SIZE,
        placePos.y - cc.y * CHUNK_SIZE,
        placePos.z - cc.z * CHUNK_SIZE
    );


    c->setBlock(local, type);
    markChunkAndNeighborsDirty(cc);
}

Chunk* World::getChunkAtWorld(const glm::ivec3& worldPos)   
{
	ChunkCoord chunkPos = worldToChunk(glm::ivec3(worldPos.x, worldPos.y, worldPos.z));
   
    Chunk* c = getChunk(chunkPos);
    if (!c) return nullptr;
    return c;

}

bool World::raycast(const glm::dvec3& origin, const glm::vec3& direction, float maxDist, RaycastHit& outHit)
{
    glm::dvec3 dir = glm::normalize(glm::dvec3(direction));

    glm::ivec3 block = glm::ivec3(glm::floor(origin));

    glm::dvec3 deltaDist = glm::abs(glm::dvec3(
        dir.x != 0.0 ? 1.0 / dir.x : 1e30,
        dir.y != 0.0 ? 1.0 / dir.y : 1e30,
        dir.z != 0.0 ? 1.0 / dir.z : 1e30
    ));

    glm::ivec3 step;
    glm::dvec3 sideDist;

    for (int i = 0; i < 3; ++i)
    {
        if (dir[i] < 0.0)
        {
            step[i] = -1;
            sideDist[i] = (origin[i] - double(block[i])) * deltaDist[i];
        }
        else
        {
            step[i] = 1;
            sideDist[i] = (double(block[i]) + 1.0 - origin[i]) * deltaDist[i];
        }
    }

    double traveled = 0.0;

    while (traveled <= maxDist)
    {
        if (isSolidBlockWorld(block))
        {
            outHit.block = block;
            return true;
        }

        // Find nearest axis
        if (sideDist.x < sideDist.y)
        {
            if (sideDist.x < sideDist.z)
            {
                traveled = sideDist.x;
                sideDist.x += deltaDist.x;
                block.x += step.x;
                outHit.normal = glm::ivec3(-step.x, 0, 0);
            }
            else
            {
                traveled = sideDist.z;
                sideDist.z += deltaDist.z;
                block.z += step.z;
                outHit.normal = glm::ivec3(0, 0, -step.z);
            }
        }
        else
        {
            if (sideDist.y < sideDist.z)
            {
                traveled = sideDist.y;
                sideDist.y += deltaDist.y;
                block.y += step.y;
                outHit.normal = glm::ivec3(0, -step.y, 0);
            }
            else
            {
                traveled = sideDist.z;
                sideDist.z += deltaDist.z;
                block.z += step.z;
                outHit.normal = glm::ivec3(0, 0, -step.z);
            }
        }
    }

    return false;
}

BlockRef World::getBlockRef(glm::ivec3 worldPos)
{
    BlockRef ref{};

    int cx = floorDiv(worldPos.x, CHUNK_SIZE);
    int cy = floorDiv(worldPos.y, CHUNK_SIZE);
    int cz = floorDiv(worldPos.z, CHUNK_SIZE);

    Chunk* c = getChunk({ cx, cy, cz });
    ref.chunk = c;
    if (!c) return ref;
    ref.local = worldToLocal(worldPos.x, worldPos.y, worldPos.z);

    return ref;
}

void World::markChunkAndNeighborsDirty(const ChunkCoord& chunkPos)
{
    Chunk* c = getChunk(chunkPos);
    if (c) markDirty(c);
    else if (unloadedChunks.contains(chunkPos)) reviveChunk(chunkPos);

    for (const glm::vec3& d : FACE_DIRS)
    {
        glm::ivec3 neighborPos = chunkPos + d;
        ChunkCoord nCoord = { neighborPos.x, neighborPos.y, neighborPos.z };

        Chunk* n = getChunk(nCoord);
        if (n) markDirty(n);
        else if (unloadedChunks.contains(nCoord)) reviveChunk(nCoord);
    }

}

bool World::isSolidBlockWorld(const glm::ivec3& worldPos)
{ 
    return !isAirWorld(worldPos.x, worldPos.y, worldPos.z);
}

// ===========================
// ASYNC CHUNK LOADING SYSTEM
// ===========================

/**
 * Build a loading plan for chunks around the player.
 * Runs on a background thread to avoid blocking the main thread.
 * Sorts chunks by priority (distance, view direction, ground proximity).
 */
void World::buildPlanTask(std::shared_ptr<World::LoadingPlan> plan, glm::vec3 viewDir, uint32_t seed) {
    ChunkCoord center = {
        (int)std::floor(plan->playerPos.x / CHUNK_SIZE),
        (int)std::floor(plan->playerPos.y / CHUNK_SIZE),
        (int)std::floor(plan->playerPos.z / CHUNK_SIZE)
    };
    int chunksToBottom = std::min(MAX_NEG_RENDER_RADIUS_Y, center.y);

    viewDir.y = 0.0f;
    if (glm::dot(viewDir, viewDir) > 0.0001f) viewDir = glm::normalize(viewDir);

	// reserve chunks to avoid reallocations
    // Calculate the amount of chunks in the render distance
    int height = RENDER_RADIUS + POS_RENDER_RADIUS_Y + chunksToBottom + 1;
    int reserveAmount = (int)(PI * RENDER_RADIUS * RENDER_RADIUS * height) + 10; // Slight padding to counter rounding errors

    plan->neededOrdered.reserve(reserveAmount);
    plan->neededSet.reserve(reserveAmount);

    for (int x = -RENDER_RADIUS; x <= RENDER_RADIUS; ++x) {
        for (int z = -RENDER_RADIUS; z <= RENDER_RADIUS; ++z) {
            if (x * x + z * z > RENDER_RADIUS * RENDER_RADIUS) continue;
            for (int y = -chunksToBottom; y <= POS_RENDER_RADIUS_Y; ++y) {
                ChunkCoord c{ center.x + x, center.y + y, center.z + z };

                // Calculate the distance from the player to the chunk
                glm::dvec3 chunkPosVec = { c.x, c.y, c.z };
                glm::dvec3 diff = plan->playerPos / (double)CHUNK_SIZE - chunkPosVec; // Subtract the chunk space player pos from the chunk pos
                int squaredDistance = (int)(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

                Chunk* cp = getChunk(c);
                if (cp) { // Make sure that the chunk is valid
                    int LOD_Level = cp->getLODlevel(squaredDistance);
                    if (cp->chunkLOD != LOD_Level) {
                        plan->needsLODRebuild.push_back(c);
                        cp->chunkLOD = LOD_Level; // Set the LOD level early to avoid recomputation
                    }
                }

                plan->neededOrdered.push_back(c);
                plan->neededSet.insert(c);
            }
        }
    }

    // Pre-calculate scores to minimize math in sort
    struct ScoredCoord { ChunkCoord c; double score; };
    std::vector<ScoredCoord> scored;
    scored.reserve(plan->neededOrdered.size());

    // Score each chunk for priority (lower = higher priority)
    for (const auto& c : plan->neededOrdered) {
        double dcx = (double)c.x * CHUNK_SIZE + CHUNK_SIZE / 2.0 - plan->playerPos.x;
        double dcy = (double)c.y * CHUNK_SIZE + CHUNK_SIZE / 2.0 - plan->playerPos.y;
        double dcz = (double)c.z * CHUNK_SIZE + CHUNK_SIZE / 2.0 - plan->playerPos.z;
        double distSq = dcx * dcx + dcy * dcy + dcz * dcz;

        float facing = 0.0f;
        double distFlatSq = dcx * dcx + dcz * dcz;
        if (distFlatSq > 0.0001) {
            facing = (float)(dcx * viewDir.x + dcz * viewDir.z) / (float)sqrt(distFlatSq);
        }
        
        // Bonus for chunks below player (load ground first)
        float groundBonus = (dcy <= 0.0) ? (float)(-dcy * GROUND_BIAS_WEIGHT) : 0.0f;
        
        // Final score: distance - bonuses (lower is better)
        scored.push_back({ c, distSq - (double)facing * VIEW_BIAS_WEIGHT - (double)groundBonus });
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredCoord& a, const ScoredCoord& b) {
        return a.score < b.score;
    });

    for (size_t i = 0; i < scored.size(); ++i) plan->neededOrdered[i] = scored[i].c;
    plan->ready = true;
}

/**
 * Update loaded chunks based on player position.
 * 
 * 6-PHASE PROCESS:
 * 1. Kick off async plan building if player moved significantly
 * 2. Check if plan is ready and activate it
 * 3. Time-sliced chunk loading (batched per frame)
 * 4. Time-sliced chunk unloading (iterator-based)
 * 5. Periodic pruning of unloaded chunk registry
 * 6. Rebuilding of some of the LODs for the remaining chunks
 * 
 */
void World::updateLoadedChunks(const glm::dvec3& playerPos, glm::vec3 viewDir, const uint32_t seed)
{
    // Do phase 2 first to avoid losing built plans
    // PHASE 2: Poll for plan completion and activate
    {
        std::lock_guard<std::mutex> lock(planMutex);
        if (nextPlan && nextPlan->ready) {
            activePlan = nextPlan;
            nextPlan.reset();
            nextLoadIndex = 0;
            radiusFullApplied = false;
            
            // Build the unload queue safely (snapshot)
            unloadQueue.clear();
            unloadQueue.reserve(chunks.size() / 10);
            for (const auto& pair : chunks) {
                if (!activePlan->neededSet.contains(pair.first)) {
                    unloadQueue.push_back(pair.first);
                }
            }
            nextUnloadIndex = 0;
            isUnloading = true;
        }
    }

    // PHASE 1: Background planning if moved significantly
    double dx = playerPos.x - lastUpdatePos.x;
    double dy = playerPos.y - lastUpdatePos.y;
    double dz = playerPos.z - lastUpdatePos.z;
    bool moved = (dx*dx + dy*dy + dz*dz >= REPLAN_DISTANCE_SQ); // Only replan every 4+ blocks

    if ((moved || !activePlan) && !planInProgressBar) {
        planInProgressBar = true;
        lastUpdatePos = playerPos;
        auto plan = std::make_shared<LoadingPlan>();
        plan->playerPos = playerPos;
        nextPlan = plan;

        std::thread([this, plan, viewDir, seed]() {
            buildPlanTask(plan, viewDir, seed);
            std::lock_guard<std::mutex> lock(planMutex);
            planInProgressBar = false;
        }).detach();
    }

    if (!activePlan) return; // No plan yet

    // PHASE 3: Time-sliced loading (batched)
    if (!radiusFullApplied) {
        int loadedThisFrame = 0;
        while (nextLoadIndex < activePlan->neededOrdered.size() && loadedThisFrame < MAX_CHUNKS_LOADED_PER_FRAME) {
            const ChunkCoord& coord = activePlan->neededOrdered[nextLoadIndex++];
            if (!chunks.contains(coord) && !unloadedChunks.contains(coord)) {
                loadChunk(coord, seed);
                ++loadedThisFrame;
            }
        }
        if (nextLoadIndex >= activePlan->neededOrdered.size()) radiusFullApplied = true;
    }

    // PHASE 4: Time-sliced unloading (queue-based)
    if (isUnloading) {
        int checked = 0;
        // Unload chunks from queue (O(1) lookup per chunk)
        while (checked < UNLOAD_CHECKS_PER_FRAME && nextUnloadIndex < unloadQueue.size()) {
            ChunkCoord c = unloadQueue[nextUnloadIndex++];
            
            auto it = chunks.find(c);
            if (it != chunks.end()) {
                unloadChunk(it);
            }
            checked++;
        }
        if (nextUnloadIndex >= unloadQueue.size()) {
            isUnloading = false;
            unloadQueue.clear(); // Free memory
        }
    }

    // PHASE 5: Periodic pruning of unloaded chunks registrar
    static int pruneCounter = 0;
    if (++pruneCounter >= PRUNE_INTERVAL_FRAMES) {
        pruneCounter = 0;
        // Time-slice the pruning if needed
        for (auto it = unloadedChunks.begin(); it != unloadedChunks.end(); ) {
            if (!activePlan->neededSet.contains(it->first)) it = unloadedChunks.erase(it);
            else ++it;
        }
    }

    // Rebuilding of some of the LODs for the remaining chunks
    for (const ChunkCoord& cc : activePlan->needsLODRebuild) {
        auto it = chunks.find(cc);
        if (it != chunks.end() && it->second) {
            Chunk* c = it->second.get();
            c->rebuildNeighbourLODs(playerPos); // Update the LOD's
            markDirty(c);  // Mark dirty, so the updates would take place
            
            // Also mark its neighbors dirty, so their boundaries rebuild with the new LOD
            for (const glm::ivec3& dir : FACE_DIRS) {
                ChunkCoord nCoord = { cc.x + dir.x, cc.y + dir.y, cc.z + dir.z };
                auto nIt = chunks.find(nCoord);
                if (nIt != chunks.end() && nIt->second) {
                    nIt->second->rebuildNeighbourLODs(playerPos);
                    markDirty(nIt->second.get());
                }
            }
        }
    }
    activePlan->needsLODRebuild.clear();
}


void World::loadChunk(const ChunkCoord& coord, int seed, bool immediate)
{
    if (!immediate && useThreadedChunks && chunkThreads) {
        // Queue for threaded generation
        if (!chunkThreads->isPendingGeneration(coord)) {
            chunkThreads->queueGeneration(coord, seed, this);
        }
    } else {
        // Synchronous loading (fallback)
        auto chunk = std::make_shared<Chunk>(coord, this, seed);

        chunks.emplace(coord, chunk);
        chunk->markDirty(Dirty_Mesh);

        // Remove from unloadedChunks set
        if (unloadedChunks.contains(coord)) {
            unloadedChunks.erase(coord);
        }

        // Mark all neighbors dirty as well
        for (const glm::ivec3& dir : FACE_DIRS)
        {
            ChunkCoord nCoord = { coord.x + dir.x, coord.y + dir.y, coord.z + dir.z };
            auto nIt = chunks.find(nCoord);
            if (nIt != chunks.end() && nIt->second)
            {
                markDirty(nIt->second.get());
            }
        }
    }
}

void World::finalizeChunkLoad(const ChunkCoord& coord, std::shared_ptr<Chunk> chunk)
{
    if (!chunk) return;
    
    Chunk* rawChunk = chunk.get();

    // Check if the chunk is COMPLETELY EMPTY (all air)
    // If it is, we don't even add it to the chunks map or queue a mesh job.
    // We just put it straight into unloadedChunks. This prevents thousands of air chunks
    // from bloating the render loop during altitude changes.
    if (rawChunk->isAirOnly()) {
        unloadedChunks[coord] = ChunkState::EMPTY;
        return;
    }

    chunks.emplace(coord, std::move(chunk));
    
    if (unloadedChunks.contains(coord)) {
        unloadedChunks.erase(coord);
    }

    // Mark this chunk dirty - add to FRONT so it generates its first mesh quickly
    if (!rawChunk->isDirty(Dirty_Mesh)) {
        rawChunk->markDirty(Dirty_Mesh);
    }
    if (!rawChunk->isInDirtyList) {
        rawChunk->isInDirtyList = true;
        dirtyChunks.push_front(coord);
    }

    // Neighbors now have a new friend, they must rebuild their boundaries
    for (const glm::ivec3& dir : FACE_DIRS)
    {
        ChunkCoord nCoord = { coord.x + dir.x, coord.y + dir.y, coord.z + dir.z };
        auto nIt = chunks.find(nCoord);
        if (nIt != chunks.end() && nIt->second)
        {
            markDirty(nIt->second.get());
        }
    }
}

void World::finalizeChunkMesh(const ChunkCoord& coord, ChunkMesh&& mesh)
{
    Chunk* c = getChunk(coord);
    if (!c) return;

    // Apply the mesh built by the worker
    c->applyMesh(std::move(mesh));

    // Handle auto-unloading of EMPTY AIR chunks (no solid blocks at all)
    // This is safe and helps performance. We still keep HIDDEN_SOLIDS loaded for now.
    if (c->solidBlockCount == 0 && !unloadedChunks.contains(coord))
    {
        auto mapIt = chunks.find(coord);
        if (mapIt != chunks.end()) {
            unloadChunk(mapIt);
            unloadedChunks[coord] = ChunkState::EMPTY;
        }
        return;
    }

    // If a neighbor finished loading while we were meshing, we might be dirty again.
    // We'll let the next rebuildDirtyChunks() pick it up.
    bool wasMeshed = c->hasMesh();
    c->uploadMesh();
    
    if (!wasMeshed && c->hasMesh()) {
        activeMeshes.push_back(c);
    } else if (wasMeshed && !c->hasMesh()) {
        activeMeshes.erase(std::remove(activeMeshes.begin(), activeMeshes.end(), c), activeMeshes.end());
    }
    
    // Note: clearDirty(Dirty_Mesh) is actually handled inside uploadMesh() or we do it here
    c->clearDirty(Dirty_Mesh);

    if (c->meshDirtyDuringBuild) {
        c->meshDirtyDuringBuild = false;
        markDirty(c);
    }
}


void World::processThreadedWork()
{
    if (!chunkThreads) return;
    
    // Process completed generations
    chunkThreads->processCompletedGenerations(this, MAX_CHUNKS_LOADED_PER_FRAME);
    
    // Process completed meshes  
    chunkThreads->processCompletedMeshes(this, MAX_CHUNK_UPDATES_PER_FRAME);
}


void World::reviveChunk(const ChunkCoord& coord, bool immediate)
{
    if (unloadedChunks.contains(coord)) {
        loadChunk(coord, worldSeed, immediate);
    }
}

World::ChunkMap::iterator World::unloadChunk(ChunkMap::iterator it)
{
    ChunkCoord coord = it->first;

    // Cancel any pending threaded jobs for this chunk
    if (chunkThreads) {
        chunkThreads->cancelJobs(coord);
    }

    // Neighbors now have a lost friend, they must rebuild their boundaries
    for (const glm::ivec3& dir : FACE_DIRS)
    {
        ChunkCoord nCoord = { coord.x + dir.x, coord.y + dir.y, coord.z + dir.z };
        auto nIt = chunks.find(nCoord);
        if (nIt != chunks.end() && nIt->second) markDirty(nIt->second.get());
    }

    // Remove from active meshes list if present
    activeMeshes.erase(std::remove(activeMeshes.begin(), activeMeshes.end(), it->second.get()), activeMeshes.end());

    return chunks.erase(it);
}


std::array<Chunk*, 6> World::getNeighbors(Chunk* c)
{
    ChunkCoord cc = c->getChunkPos();
    return {
        getChunk({cc.x + 1, cc.y    , cc.z    }),
        getChunk({cc.x - 1, cc.y    , cc.z    }),
        getChunk({cc.x    , cc.y + 1, cc.z    }),
        getChunk({cc.x    , cc.y - 1, cc.z    }),
        getChunk({cc.x    , cc.y    , cc.z + 1}),
        getChunk({cc.x    , cc.y    , cc.z - 1})
    };
}

Chunk* World::getChunk(const ChunkCoord& c) {
    auto it = chunks.find(c);
    return (it != chunks.end()) ? it->second.get() : nullptr;
}


bool World::isAirWorld(int wx, int wy, int wz) {
    ChunkCoord cc = worldToChunk(glm::ivec3(wx, wy, wz));
    auto it = chunks.find(cc);
    if (it == chunks.end()) {
        // Check if it's a hidden solid chunk
        auto unloadedIt = unloadedChunks.find(cc);
        if (unloadedIt != unloadedChunks.end()) {
            return unloadedIt->second == ChunkState::EMPTY;
        }
        return true; 
    }

    glm::ivec3 local = worldToLocal(wx, wy, wz);
    return it->second->isAir(local);
}


glm::ivec3 World::worldToLocal(int wx, int wy, int wz) {
    ChunkCoord cc = worldToChunk({ wx, wy, wz });

    return {
        wx - cc.x * CHUNK_SIZE,
        wy - cc.y * CHUNK_SIZE,
        wz - cc.z * CHUNK_SIZE
    };
}

void World::setBlockWorld(int wx, int wy, int wz, BlockType type) {
    ChunkCoord cc = worldToChunk({ wx, wy, wz });
    Chunk* c = getChunk(cc);
    if (!c) return;

    glm::ivec3 local = worldToLocal(wx, wy, wz);
    c->setBlock(local, type);
    markDirty(c);

    // Neighbor updates
    for (auto dir : FACE_DIRS) {
        ChunkCoord nc = worldToChunk({ wx + dir.x, wy + dir.y, wz + dir.z });
        if (Chunk* n = getChunk(nc))
            markDirty(n);
    }
}

Chunk* World::getChunkFromWorldPos(const glm::ivec3& worldPos) {
    int cx = floorDiv(worldPos.x, CHUNK_SIZE);
    int cy = floorDiv(worldPos.y, CHUNK_SIZE);
    int cz = floorDiv(worldPos.z, CHUNK_SIZE);

    return getChunk({ cx, cy, cz });
}

void World::setRandSeed() {
    worldSeed = Terrain::setRandSeed(this);
}


bool World::isChunkHiddenSolid(const ChunkCoord& c)
{
    auto it = unloadedChunks.find(c);
    if (it != unloadedChunks.end()) {
        return it->second == ChunkState::HIDDEN_SOLID;
    }
    return false;
}

// Check if a bounding box at a given position collides with any solid blocks in the world.
bool World::checkCollision(const glm::dvec3& pos, const glm::vec3& dimensions)
{
    glm::ivec3 min = glm::floor(pos - glm::dvec3(dimensions.x / 2.0, 0.0, dimensions.z / 2.0));
    glm::ivec3 max = glm::floor(pos + glm::dvec3(dimensions.x / 2.0, dimensions.y, dimensions.z / 2.0));

    for (int x = min.x; x <= max.x; x++) {
        for (int y = min.y; y <= max.y; y++) {
            for (int z = min.z; z <= max.z; z++) {
                if (isSolidBlockWorld({x, y, z})) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Resolve collisions for an entity at a given position with a given velocity and dimensions.
void World::resolveCollision(glm::dvec3& pos, glm::vec3& velocity, const glm::vec3& dimensions, bool& onGround)
{
    onGround = false;
    glm::dvec3 nextPos = pos;
    float halfWidth = dimensions.x / 2.0f;
    float halfDepth = dimensions.z / 2.0f;

    // Resolve Y
    nextPos.y += velocity.y;
    if (checkCollision(nextPos, dimensions)) {
        if (velocity.y < 0) onGround = true;
        velocity.y = 0;
        nextPos.y = pos.y;
    }
    pos.y = nextPos.y;

    // Resolve X
    nextPos.x += velocity.x;
    if (checkCollision(nextPos, dimensions)) {
        velocity.x = 0;
        nextPos.x = pos.x;
    }
    pos.x = nextPos.x;

    // Resolve Z
    nextPos.z += velocity.z;
    if (checkCollision(nextPos, dimensions)) {
        velocity.z = 0;
        nextPos.z = pos.z;
    }
    pos.z = nextPos.z;
}

