#include "pch.h"

#include "commands.hpp"
#include "OriginBlock.hpp"  // For Engine

std::map<std::string, size_t> Commands::validCommands = {
    {"position", 1},
    {"teleport", 2},
    {"tp", 2},
    {"seed", 3},
    {"setseed", 4},
    {"loadchunk", 5},
    {"help", 6},
    {"positionchunk", 7},
	{"poschunk", 7 },
	{"generateheightmap", 8},
    {"tf", 9},
    {"toggleflying", 9},
    {"playerdata", 10}, 
    {"worlddata", 11},
    {"clearchat", 12}
};

size_t Commands::getCommandID(std::string command, std::vector<std::string>& outArgs) {
    const std::string prefix = "/";
    size_t ID = 0;
    outArgs.clear();

    // Trim leading/trailing spaces
    command.erase(0, command.find_first_not_of(" \t"));
    command.erase(command.find_last_not_of(" \t") + 1);

    // Check and remove prefix
    if (command.compare(0, prefix.length(), prefix) != 0) {
        return ID;  // No prefix, ignore
    }
    command.erase(0, prefix.length());

    // Split into words (command + args)
    std::istringstream iss(command);
    std::string word;
    std::vector<std::string> parts;
    while (iss >> word) {
        // Convert to lowercase for case-insensitivity
        std::transform(word.begin(), word.end(), word.begin(),
            [](unsigned char c) { return std::tolower(c); });
        parts.push_back(word);
    }

    if (parts.empty()) return ID;

    // First part is the command
    std::string cmd = parts[0];
    auto it = validCommands.find(cmd);
    if (it == validCommands.end()) return ID;

    ID = it->second;

    // Remaining parts are args
    if (parts.size() > 1) {
        outArgs.assign(parts.begin() + 1, parts.end());
    }

    return ID;
}

void Commands::executeCommand(size_t ID, const std::vector<std::string>& args, const std::string& inCMD, Engine& engine) {
    std::string result;
    result += "> ";

    switch (ID) {
    case 0: break;  // Invalid, do nothing
    case 1: {  // /position
            glm::dvec3 p = engine.playerCamera.position;
            result += "Position: (" + std::to_string(static_cast<int>(p.x)) + ", " +
                std::to_string(static_cast<int>(p.y)) + ", " +
                std::to_string(static_cast<int>(p.z)) + ")";
            break;
    }
    case 2: {  // /teleport x y z or /tp x y z
        if (args.size() != 3) {
            result += "Usage: /tp x y z";
            break;
        }
        try {
            double newPos[3] = {0.0};

            for (int i = 0; i < 3; i++) {
                if (args[i].size() >= 2) {
                    std::string newArg = args[i];

					// Check for relative position indicators
                    if (args[i][0] == '~' || args[i][0] == '...') {
						newArg.erase(0, 1); // Remove '~' or '...'
                        newPos[i] = engine.playerCamera.position[i];
                    }
                    if (!newArg.empty()) { newPos[i] += std::stod(newArg); }
                }
                else if (args[i][0] == '~') {
                    newPos[i] = engine.playerCamera.position[i];
                }
                else {
					result += "Argument " + std::to_string(i + 1) + " is invalid ";
                }
            }
            
            engine.playerCamera.position = glm::dvec3(newPos[0], newPos[1], newPos[2]);
            result += "Teleported to (" + std::to_string(newPos[0]) + ", " + std::to_string(newPos[1]) + ", " + std::to_string(newPos[2]) + ")";
        }
        catch (...) {
            result += "Invalid coordinates";
        }
        break;
    }
    case 3: {  // /seed
        result += "Seed: " + std::to_string(engine.overworld.worldSeed);
        break;
    }
    case 4: {  // /setseed n
        if (args.size() != 1) {
            result += "Usage: /setseed number";
            break;
        }
        try {
            uint32_t newSeed = std::stoul(args[0]);
            engine.overworld.worldSeed = newSeed;
            engine.overworld.reloadAllChunks(true);  // Regenerate with new seed
            result += "Seed set to " + args[0] + "  Regenerating world...";
        }
        catch (...) {
            result += "Invalid seed (must be a positive integer)";
        }
        break;
    }
    case 5: { // /loadchunk x y z
        if (args.size() == 1 && args[0] == "~") {
            glm::dvec3 p = engine.playerCamera.position;
            ChunkCoord cc = engine.overworld.worldToChunk(glm::ivec3(static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(p.z)));
            engine.overworld.loadChunk(cc, engine.overworld.worldSeed, true);
            
            result += "Loaded chunk at (" + std::to_string(cc.x) + ", " + std::to_string(cc.y) + ", " + std::to_string(cc.z) + ")";
            break;
        }
        if (args.size() != 3) {
            result += "Usage: /loadchunk player[x y z]";
            break;
        }
        try {
            ChunkCoord cc = { std::stoi(args[0]) , std::stoi(args[1]) , std::stoi(args[2]) };
            engine.overworld.loadChunk(cc, engine.overworld.worldSeed, true);
            result += "Loaded chunk at (" + args[0] + ", " + args[1] + ", " + args[2] + ")";
        }
        catch (...) {
            result += "Invalid chunk coordinates";
        }
        break;
    }
	case 6: { // /help
        if (args.size() != 0) {
            result += "/help does not take any arguments!";
            break;
        }
        result += "Available commands:\n";
        for (auto& c : validCommands) {
            result += "/" + c.first + "\n";
        }
        break;
    }
	case 7: { // /positionchunk or /poschunk
        if (args.size() != 0) {
            result += "/positionchunk does not take any arguments!";
            break;
        }
        glm::dvec3 p = engine.playerCamera.position;
        ChunkCoord cc = engine.overworld.worldToChunk(glm::ivec3(static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(p.z)));
        result += "Current chunk: (" + std::to_string(cc.x) + ", " + std::to_string(cc.y) + ", " + std::to_string(cc.z) + ")";

        break;
    }
	case 8: { // /generateheightmap startX startZ countX countZ
        if (args.size() != 4) {
            result += "Usage: /generateheightmap startX startZ countX countZ";
            break;
        }

        std::string debugFilenameStr = "heightmaps/heightmap" + std::to_string(engine.overworld.worldSeed) + ".png";
        const char* debugFilename = debugFilenameStr.c_str();
        engine.terrain.writeChunkHeightmapPNG(std::stoi(args[0]), std::stoi(args[1]), std::stoi(args[2]), std::stoi(args[3]), CHUNK_SIZE, engine.overworld.worldSeed, debugFilename);
        result += "Generated heightmap: " + debugFilenameStr;
        break;
    }
    case 9: { // /tf or /toggleflying
        if (args.size() != 0) {
            result += "/toggleflying does not take any arguments!";
            break;
        }
        engine.playerCamera.isFlying = !engine.playerCamera.isFlying;
        result += "Flying mode " + std::string(engine.playerCamera.isFlying ? "enabled" : "disabled");
        break;
    }
    case 10: { // /playerdata
        if (args.size() != 0) {
            result += "/playerdata does not take any arguments!";
            break;
        }
        glm::dvec3 p = engine.playerCamera.position;
		glm::vec3 r = engine.playerCamera.rotation;
        result += "Player Data:\n";
        result += "Position: (" + std::to_string(p.x) + ", " + std::to_string(p.y) + ", " + std::to_string(p.z) + ")\n";
		result += "Rotation (yaw, pitch, roll): (" + std::to_string(r.x) + ", " + std::to_string(r.y) + ", " + std::to_string(r.z) + ")\n";
        result += "Flying: " + std::string(engine.playerCamera.isFlying ? "Yes" : "No") + "\n";
        break;
	}
    case 11: { // /worlddata
        if (args.size() != 0) {
            result += "/worlddata does not take any arguments!";
            break;
        }
        result += "World Data:\n";
		result += "Render Distance: [" + std::to_string(RENDER_RADIUS) + ", +" + std::to_string(POS_RENDER_RADIUS_Y) + ", -" + std::to_string(MAX_NEG_RENDER_RADIUS_Y) + "] chunks\n";
        result += "Seed: " + std::to_string(engine.overworld.worldSeed) + "\n";
        result += "Loaded Chunks: " + std::to_string(engine.overworld.chunks.size()) + "\n";
        break;
	}
    case 12: { // /clearchat
        if (args.size() != 0) {
            result += "/clearchat does not take any arguments!";
            break;
        }
        engine.clearChat();
        break;
    }
    default:
        result += "Command '" + inCMD + "' is unknown\nIf you need help, type /help";
        break;
    }

    if (!result.empty()) {
        engine.printToChat(result, false,  true);
    }
}