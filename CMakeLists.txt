# CMakeList.txt : CMake project for OriginBlock , include source and define
# project specific logic here.
#


cmake_minimum_required(VERSION 3.16)
project(OriginBlock)

# Make sure to use the correct architecture for Apple Silicon (arm64) when building on macOS.
if (APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Configure compiler and linker flags for MSVC
if (MSVC)
    get_filename_component(MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

    set(CMAKE_VS_DEBUGGER_ENVIRONMENT
        "PATH=${MSVC_BIN_DIR};%PATH%"
    )

    # -------------------------------------------------------------------------
    # 1. Clean up default Debug flags for ASan compatibility
    # -------------------------------------------------------------------------
    string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/ZI" "/Zi" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

    # -------------------------------------------------------------------------
    # 2. Development Configuration (Fastest Build Speed & Debugging)
    # -------------------------------------------------------------------------
    add_compile_options(
        $<$<CONFIG:Debug>:/fsanitize=address>   # Keep ASan active
        $<$<CONFIG:Debug>:/MP>                  # Multi-process compilation
        $<$<CONFIG:Debug>:/Gm->                 # Disable minimal rebuild (faster with /MP)
        $<$<CONFIG:Debug>:/Zc:preprocessor>     # Modern, fast preprocessor
    )
    
    add_link_options(
        $<$<CONFIG:Debug>:/fsanitize=address>
        $<$<CONFIG:Debug>:/STACK:16777216>
    )

    # -------------------------------------------------------------------------
    # 3. Release Configuration (Fastest Voxel Game Runtime Speed)
    # -------------------------------------------------------------------------
    add_compile_options(
        $<$<CONFIG:Release>:/O2>                # Maximize speed
        $<$<CONFIG:Release>:/Ot>                # Favor fast code over small binary size
        $<$<CONFIG:Release>:/Oi>                # Enable intrinsic math functions
        $<$<CONFIG:Release>:/fp:fast>           # Relax IEEE rules for fast noise generation
        $<$<CONFIG:Release>:/arch:AVX2>         # Target AVX2 vector instructions
        $<$<CONFIG:Release>:/GL>                # Whole Program Optimization (LTCG compile step)
    )

    add_link_options(
        $<$<CONFIG:Release>:/LTCG>              # Link-Time Code Generation (LTCG link step)
    )
endif()



# -----------------------------------------------------------------------------
# Vendored Dependencies (FetchContent with offline/zip fallback)
# -----------------------------------------------------------------------------
include(FetchContent)

# GLFW (windowing & input)
# Priority: local deps/glfw/ (offline-friendly) → GitHub download (first run)
set(_glfw_local "${CMAKE_SOURCE_DIR}/deps/glfw")
if(EXISTS "${_glfw_local}/CMakeLists.txt")
    message(STATUS "GLFW: using vendored source at deps/glfw")
    FetchContent_Declare(glfw SOURCE_DIR "${_glfw_local}")
else()
    message(STATUS "GLFW: fetching from GitHub (pin: tag 3.4)")
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
endif()
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# SFML — commented out until audio/image loading is actively used
# When re-enabling, also uncomment the SFML includes in include/pch.h
# and add sfml-system (+ sfml-audio etc.) back to target_link_libraries.
#
# set(SFML_DIR "C:/SFML-2.6.0/lib/cmake/SFML")
# find_package(SFML 2.6 COMPONENTS system window graphics audio REQUIRED)

# Find OpenGL
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# Add executable
add_executable(OriginBlock "src/OriginBlock.cpp" "src/glad/glad.c" "src/shader.cpp" "src/inputs.cpp" "src/textureManager.cpp" "src/texture.cpp" "src/stb_image.cpp" "src/chunk.cpp" "src/Worlds.cpp" "src/terrain.cpp" "src/noise.cpp" "src/commands.cpp" "src/ThreadedChunkSystem.cpp" "src/TextRenderer.cpp" "src/pch.cpp" "src/biome.cpp" "src/voronoi.cpp" "src/heightfield.cpp" "src/column_cache.cpp" "src/vegetation.cpp" "src/debug_export.cpp")

# Define DEBUG_EXPORT_ENABLED for Debug builds
# Uses a generator expression so it works correctly with multi-config generators (e.g. MSVC/Ninja Multi-Config)
#target_compile_definitions(OriginBlock PRIVATE $<$<CONFIG:Debug>:DEBUG_EXPORT_ENABLED>)
target_compile_definitions(OriginBlock PRIVATE $<$<CONFIG:Release>:DEBUG_EXPORT_ENABLED>) # Enable debug export in Release builds for testing purposes. Remove this line in production builds.

# Include headers
target_include_directories(OriginBlock PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_precompile_headers(OriginBlock PRIVATE ${PROJECT_SOURCE_DIR}/include/pch.h)


# Link libraries
# Note: 'glfw' target is provided by FetchContent above — no IMPORTED lib needed.
# sfml-system commented out until SFML is re-enabled.
target_link_libraries(OriginBlock
    # sfml-system
    OpenGL::GL
    glfw
    Threads::Threads
)

add_custom_command(TARGET OriginBlock POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:OriginBlock>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/res
        $<TARGET_FILE_DIR:OriginBlock>/res
)


# TODO: Add tests and install targets if needed.
