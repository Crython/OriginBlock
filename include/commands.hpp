#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include "Worlds.hpp"

// Forward declaration to avoid circular include
class Engine;

class Commands {
public:
	Commands() = default;
	~Commands() = default;
	void executeCommand(const std::string& command, Engine& engine);
private:
    static std::map<std::string, size_t> validCommands;  // Static for class-wide access

    size_t getCommandID(std::string command, std::vector<std::string>& outArgs);  // Now extracts args
    void executeCommandFromID(size_t ID, const std::vector<std::string>& args, const std::string& inCMD, Engine& engine);
};

#endif // COMMANDS_HPP