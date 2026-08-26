# CMakeList.txt : CMake project for OriginBlock
cmake_minimum_required(VERSION 3.16)
project(OriginBlock)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------------------------------------------------------
# 1. Platform-Specific Compiler & Linker Flags
# -----------------------------------------------------------------------------
if (MSVC)
    # --- Windows / MSVC Configuration ---
    # Clean up default Debug flags for ASan compatibility
    string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/ZI" "/Zi" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

    # Debug Configuration
    add_compile_options(
        $<$<CONFIG:Debug>:/fsanitize=address>
        $<$<CONFIG:Debug>:/MP>
        $<$<CONFIG:Debug>:/Gm->
        $<$<CONFIG:Debug>:/Zc:preprocessor>
    )
    
    add_link_options(
        $<$<CONFIG:Debug>:/fsanitize=address>
        $<$<CONFIG:Debug>:/STACK:16777216>
    )

    # Release Configuration
    add_compile_options(
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Ot>
        $<$<CONFIG:Release>:/Oi>
        $<$<CONFIG:Release>:/fp:fast>
        $<$<CONFIG:Release>:/arch:AVX2>
        $<$<CONFIG:Release>:/GL>
    )
    
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>
    )
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # --- macOS (Apple Clang) & Linux (GCC/Clang) Configuration ---
    # Debug Configuration
    add_compile_options(
        $<$<CONFIG:Debug>:-fsanitize=address>
        $<$<CONFIG:Debug>:-g>
    )
    add_link_options(
        $<$<CONFIG:Debug>:-fsanitize=address>
    )

    # Release Configuration
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffast-math>
    )
endif()

# -----------------------------------------------------------------------------
# 2. Find Dependencies
# -----------------------------------------------------------------------------
if (APPLE)
    # Append all common Homebrew installation paths so CMake can find dependencies 
    # regardless of whether the user has administrative privileges or not.
    list(APPEND CMAKE_PREFIX_PATH 
        "$ENV{HOME}/homebrew"  # Non-admin user-directory installation
        "/opt/homebrew"        # Standard installation for Apple Silicon Macs
        "/usr/local"           # Standard installation for Intel Macs
    )
endif()

find_package(SFML 2.6 COMPONENTS system window graphics REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
find_package(glfw3 REQUIRED)

# -----------------------------------------------------------------------------
# 3. Target Definition
# -----------------------------------------------------------------------------
add_executable(OriginBlock 
    "src/OriginBlock.cpp" "src/glad/glad.c" "src/shader.cpp" "src/inputs.cpp" 
    "src/textureManager.cpp" "src/texture.cpp" "src/stb_image.cpp" "src/chunk.cpp" 
    "src/Worlds.cpp" "src/terrain.cpp" "src/noise.cpp" "src/commands.cpp" 
    "src/ThreadedChunkSystem.cpp" "src/TextRenderer.cpp" "src/pch.cpp" "src/biome.cpp" 
    "src/voronoi.cpp" "src/heightfield.cpp" "src/column_cache.cpp" "src/vegetation.cpp" 
    "src/debug_export.cpp"
)

# Define DEBUG_EXPORT_ENABLED for Debug builds
target_compile_definitions(OriginBlock PRIVATE $<$<CONFIG:Debug>:DEBUG_EXPORT_ENABLED>)

# Include headers
target_include_directories(OriginBlock PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_precompile_headers(OriginBlock PRIVATE ${PROJECT_SOURCE_DIR}/include/pch.h)

# Link libraries
target_link_libraries(OriginBlock
    PRIVATE
    sfml-system
    sfml-window
    sfml-graphics
    OpenGL::GL
    glfw
    Threads::Threads
)

# -----------------------------------------------------------------------------
# 4. Post-Build Steps
# -----------------------------------------------------------------------------
add_custom_command(TARGET OriginBlock POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:OriginBlock>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/res
        $<TARGET_FILE_DIR:OriginBlock>/res
)