#ifndef PCH_HPP
#define PCH_HPP

// Precompiled header file for the project
// Include standard library headers
// Will be used to speed up compilation by precompiling commonly used headers

// Standard headers
#ifdef __cplusplus // Check if compiling as C++ (project has a c file, which can't use C++ features)

// Standard I/O streams and file handling
#include <fstream>
#include <iostream>
#include <sstream>

// Text and character utilities
#include <cctype>
#include <string>

// Containers
#include <array>
#include <deque>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Memory management
#include <memory>

// Algorithms and function objects
#include <algorithm>
#include <functional>
#include <utility>

// Numeric types and mathematics
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>

// Filesystem support
#include <filesystem>

// Error handling
#include <stdexcept>

// Multithreading and synchronization
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>



// SFML headers
#include "SFML/System.hpp"
#include <SFML/Audio.hpp>     // Sounds, music
#include <SFML/Network.hpp>   // Optional
#include <SFML/Graphics/Image.hpp> // For loading textures
// No window. GLFW is used for windowing instead

// GLM headers
#define GLM_FORCE_INLINE      // Forces MSVC to inline math functions, avoiding function call overhead.
#define GLM_FORCE_AVX2        // Forces GLM to use 256-bit SIMD intrinsics for vec4 structures.
#define GLM_FORCE_INTRINSICS  // Tells GLM to use hardware assembly for operations instead of pure C++.
#include "glm/glm/glm.hpp"
#include <glm/glm/gtc/matrix_transform.hpp>
#include <glm/glm/gtc/type_ptr.hpp>
#include <glm/glm/gtx/norm.hpp>  // For normalize, length
#include <glm/glm/gtx/exterior_product.hpp>

#endif // __cplusplus

// Glad header
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// TODO: Reference additional headers your program requires here.


#endif // PCH_HPP