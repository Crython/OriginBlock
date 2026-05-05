
#include "OriginBlock.hpp"
/*
static constexpr glm::vec3 ObjectScale = { 1.0f, 1.0f, 1.0f }; // Global scale for all objects

class FileHandler {
public:
    std::vector<FullShape> loadTrianglesFromOBJ(const std::string& filename, bool flipY);
};

// Fast power of 2 for uint64_t
uint64_t pow2(const uint8_t v1) {
	return (uint64_t)1 << (v1 & 0x3F); // Perform a left shift by v1 positions, when v1 <= 63 — equivalent to 2^v1
}
glm::vec3 normalize(const glm::vec3& v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return glm::vec3{ v.x / len, v.y / len, v.z / len };
}

// Returns all shapes in the .obj file in the correct format
std::vector<FullShape> FileHandler::loadTrianglesFromOBJ(const std::string& filename, bool flipY = false) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open OBJ: " << filename << "\n";
        return {};
    }

    std::vector<glm::vec3> globalVertices;
    std::vector<glm::vec3> globalNormals;
    std::vector<FullShape> shapes;

    FullShape currentShape;
    std::unordered_map<uint64_t, uint32_t> vertexMap;
    uint32_t localIndex = 0;

    auto resetShape = [&]() {
        currentShape = FullShape();
        vertexMap.clear();
        localIndex = 0;
        };

    resetShape();
    bool hasShape = false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;
        if (prefix.empty() || prefix[0] == '#') continue;

        if (prefix == "o" || prefix == "g") {
            if (hasShape && !currentShape.triangles.empty())
                shapes.push_back(std::move(currentShape));
            resetShape();
            hasShape = true;
            continue;
        }

        if (prefix == "v") {
            glm::vec3 v{};
            iss >> v.x >> v.y >> v.z;
			if (flipY) v.y = -v.y; // Flip Y axis if needed
			v *= ObjectScale; // Apply global scale

            globalVertices.push_back(v);
            continue;
        }

        if (prefix == "vn") {
            glm::vec3 n{};
            iss >> n.x >> n.y >> n.z;
            globalNormals.push_back(normalize(n));
            continue;
        }

        if (prefix == "f") {
            if (!hasShape) { resetShape(); hasShape = true; }

            std::vector<int> faceV, faceN;
            std::string token;
            while (iss >> token) {
                int vIdx = 0, nIdx = -1;
                if (sscanf(token.c_str(), "%d//%d", &vIdx, &nIdx) == 2) {}
                else if (sscanf(token.c_str(), "%d/%*d/%d", &vIdx, &nIdx) == 2) {}
                else if (sscanf(token.c_str(), "%d/%*d", &vIdx) == 1) { nIdx = -1; }
                else if (sscanf(token.c_str(), "%d", &vIdx) == 1) { nIdx = -1; }
                else continue;

                int gv = (vIdx < 0) ? int(globalVertices.size()) + vIdx : vIdx - 1;
                if (gv < 0 || gv >= int(globalVertices.size())) continue;

                int gn = -1;
                if (nIdx != -1) {
                    gn = (nIdx < 0) ? int(globalNormals.size()) + nIdx : nIdx - 1;
                    if (gn < 0 || gn >= int(globalNormals.size())) gn = -1;
                }

                faceV.push_back(gv);
                faceN.push_back(gn);
            }
            if (faceV.size() < 3) continue;

            auto makeKey = [](int gv, int gn)->uint64_t {
                return (uint64_t(uint32_t(gv)) << 32) | ((gn == -1) ? 0xFFFFFFFFu : uint32_t(gn));
                };

            auto getLocalIndex = [&](int gv, int gn)->uint32_t {
                uint64_t key = makeKey(gv, gn);
                auto it = vertexMap.find(key);
                if (it != vertexMap.end()) return it->second;
                Vertex vv{ globalVertices[gv], (gn == -1) ? glm::vec3{0,0,0} : globalNormals[gn] };
                currentShape.vertices.push_back(vv);
                uint32_t idx = localIndex++;
                vertexMap[key] = idx;
                return idx;
                };

            // Triangulate fan
            for (size_t i = 1; i + 1 < faceV.size(); ++i) {
                uint32_t a = getLocalIndex(faceV[0], faceN[0]);
                uint32_t b = getLocalIndex(faceV[i], faceN[i]);
                uint32_t c = getLocalIndex(faceV[i + 1], faceN[i + 1]);

                _IndexTriangle tri;
                // Swap b and c if Y is flipped to keep winding correct
                if (flipY) { tri.set(0, a); tri.set(1, c); tri.set(2, b); }
                else { tri.set(0, a); tri.set(1, b); tri.set(2, c); }
                currentShape.triangles.push_back(tri);
            }
        }
    }

    for (FullShape& sh : shapes) {
        sh.aabbMin = glm::vec3(FLT_MAX); // Max float value
		sh.aabbMax = glm::vec3(-FLT_MAX); //Max float negative value
        for (const Vertex& v : sh.vertices) {
            sh.aabbMin = glm::min(sh.aabbMin, v.pos);
            sh.aabbMax = glm::max(sh.aabbMax, v.pos);
        }

        glm::vec3 center = (sh.aabbMin + sh.aabbMax) * 0.5f;
        for (auto& v : sh.vertices) v.pos -= center;

    }
    
    if (hasShape && !currentShape.triangles.empty())
        shapes.push_back(std::move(currentShape));

    size_t totalTriangles = 0;
    for (const auto& sh : shapes) totalTriangles += sh.triangles.size();

    std::cerr << "Loaded " << shapes.size() << " shape" << ((shapes.size() == 1) ? "" : "s") << " with "
        << totalTriangles << " triangles.\n";

    return shapes;
}
*/
