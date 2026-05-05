#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "Worlds.hpp"
#include <map>
#include <string>
#include <vector>
#include <cctype>  // For std::tolower

// Forward declaration to avoid circular include
class Engine;

class Commands {
public:
    static std::map<std::string, size_t> validCommands;  // Static for class-wide access

    size_t getCommandID(std::string command, std::vector<std::string>& outArgs);  // Now extracts args
    void executeCommand(size_t ID, const std::vector<std::string>& args, const std::string& inCMD, Engine& engine);
};

#endif // COMMANDS_HPP