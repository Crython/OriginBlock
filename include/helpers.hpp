#ifndef HELPERS_HPP
#define HELPERS_HPP

#include <vector>
#include <chrono>
#include <iostream>
#include <string>

struct TimerEntry {
    std::string name;
    std::chrono::steady_clock::time_point time;
};


class Helpers {
public: // Functions

    // helpers for timing execution
    static void startTimer(const std::string& name); // Stores the start time
    static long int endTimer(const std::string& name); // Gives the passed time between start(name) and end(name) in nanoseconds (TC: O(n))
    static void clearTimers();

private: // Internal containers
    inline static std::vector<TimerEntry> startTimes;


};

// Helpers for execution timing
inline void Helpers::startTimer(const std::string& name)
{
    startTimes.push_back({ name, std::chrono::steady_clock::now() });
}

inline long int Helpers::endTimer(const std::string& name)
{
    auto it = startTimes.begin();

    while (it != startTimes.end()) {
        if (it->name == name) {
            auto duration =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - it->time
                );

            startTimes.erase(it);

            return duration.count();
        }

        ++it;
    }

    // Notify the user about the mismatch and return the error time
    std::cerr << "No matching timestamp name for '" << name << "'\n";
    return -1;
}

inline  void Helpers::clearTimers()
{
    startTimes.clear();
}



#endif