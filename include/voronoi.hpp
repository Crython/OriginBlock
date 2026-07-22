#ifndef VORONOI_HPP
#define VORONOI_HPP

#include "biome.hpp"
#include "noise.hpp"

struct VoronoiSite {
    float x, z;
    Biome::BiomeType biome;  // Assigned biome for the site
};

struct VoronoiSpatialGrid {
    std::vector<std::vector<size_t>> grid; // Indices into voronoiSites
    float startX, startZ;
    float mapSize;
    float cellSize;
    int cols, rows;

    void clear() {
        grid.clear();
        cols = rows = 0;
    }
};

class Voronoi {
public:
    /**
     * Initialize the Voronoi cell system for biome distribution.
     * Creates a spatial grid of biome sites, each with climate-based characteristics.
     *
     * @param seed World generation seed
     * @param numSites Number of Voronoi sites to create
     * @param mapSize Total size of the area to cover
     * @param startX, startZ World coordinates of the area's origin
     */
    static void initVoronoi(int seed, int numSites, float mapSize, float startX, float startZ);

    /**
     * Sample the biome at a specific cell position.
     * Uses domain-warped Voronoi nearest-neighbor with jittered boundaries.
     * Ocean biomes override Voronoi cells below the continent threshold.
     */
    static Biome::BiomeType sampleBiomeCell(int bx, int bz, int seed);

    /*
     * Sample blended biome parameters at a world position.
     * Performs bilinear interpolation between 4 neighboring biome cells.
     */
    static Biome::BiomeParams sampleBlendedBiomeParams(int worldX, int worldZ, int seed);

private:
    static std::vector<VoronoiSite> voronoiSites;
    static VoronoiSpatialGrid voronoiGrid;
};

#endif // VORONOI_HPP
