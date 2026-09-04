#ifndef HELPERS_HPP
#define HELPERS_HPP

struct TimerEntry {
    std::string name;
    std::chrono::steady_clock::time_point time;
};

// Static helper class for various things
class Helpers {
public: // Functions

    // helpers for timing execution
    static void startTimer(const std::string& name); // Stores the start time
    static long int endTimer(const std::string& name); // Gives the passed time between start(name) and end(name) in nanoseconds (TC: O(n))
    static void clearTimers();

	// Helpers for value transformation, interpolation, normalization, and smoothing
    static float clamp01(float v);
    static float remap01(float v);
    static float smoothstep(float t);
    static float slopeLimit(float delta, float maxSlope);
    static float slopeMagnitude(float h, float hx, float hz);
    static float compressHeight(float h, float center, float maxDelta);
    static float terrace(float h, float step, float strength);
    static float gammaCurve(float x, float gamma);
    static float sigmoidCurve(float x, float steepness = 10.0f);
    static void normalize3(float& a, float& b, float& c);
	static float lerp(float a, float b, float t) { return a + t * (b - a); }

private: // Internal containers
    inline static std::vector<TimerEntry> startTimes;


}; // vvv All functions are inline to avoid linker errors when included in multiple translation units

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

// -- Helpers for value transformation, interpolation, normalization, and smoothing --
// Clamps a floating-point value to ensure it stays within the range [0.0, 1.0].
inline float Helpers::clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Remaps a floating-point value from a standard range of [-1.0, 1.0] to a range of [0.0, 1.0].
inline float Helpers::remap01(float v) {
    return v * 0.5f + 0.5f; // [-1,1] -> [0,1]
}  

// Performs smooth Hermite interpolation between 0.0 and 1.0 based on an input progress variable.
inline float Helpers::smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Restricts a height delta value to stay within a specified maximum slope range.
inline float Helpers::slopeLimit(float delta, float maxSlope) {
    return std::clamp(delta, -maxSlope, maxSlope);
}

// Calculates the magnitude of the local terrain slope gradient using adjacent height samples.
inline float Helpers::slopeMagnitude(float h, float hx, float hz) {
    float dx = h - hx;
    float dz = h - hz;
    return std::sqrt(dx * dx + dz * dz);
}

// Dynamically flattens/squashes extreme terrain heights that exceed a specified threshold from the center.
inline float Helpers::compressHeight(float h, float center, float maxDelta) {
    float d = h - center;

    if (std::abs(d) <= maxDelta)
        return h;

    float sign = (d > 0.0f) ? 1.0f : -1.0f;
    float excess = std::abs(d) - maxDelta;

    // Nonlinear compression
    excess = std::sqrt(excess);

    return center + sign * (maxDelta + excess);
}

inline float Helpers::terrace(float h, float step, float strength)
{
    float base = floor(h / step) * step;
    float frac = (h - base) / step;

    // Smooth transition between steps
    frac = frac * frac * (3.0f - 2.0f * frac);

    return base + frac * step * strength;
}

inline float Helpers::gammaCurve(float x, float gamma) {
    return std::pow(x, gamma);
}

// Sigmoid function centered at 0.5 with adjustable steepness
inline float Helpers::sigmoidCurve(float x, float steepness) {
    return 1.0f / (1.0f + std::exp(-steepness * (x - 0.5f)));
}

// Normalize three floats so their sum is 1 (if sum > 0)
inline void Helpers::normalize3(float& a, float& b, float& c) {
    float sum = a + b + c;
    if (sum > 0.0f) {
        a /= sum;
        b /= sum;
        c /= sum;
    }
}



#endif