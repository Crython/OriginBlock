# CMakeList.txt : CMake project for OriginBlock , include source and define
# project specific logic here.
#


cmake_minimum_required(VERSION 3.16)
project(OriginBlock)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Configure compiler and linker flags for MSVC
if (MSVC)
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


# SFML location (adjust to your path)
set(SFML_DIR "C:/SFML-2.6.0/lib/cmake/SFML")

# Find SFML
find_package(SFML 2.6 COMPONENTS system window graphics REQUIRED)
# Find OpenGL
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# Add executable
add_executable(OriginBlock "src/OriginBlock.cpp" "src/glad/glad.c" "src/shader.cpp" "src/inputs.cpp" "src/textureManager.cpp" "src/texture.cpp" "src/stb_image.cpp" "src/chunk.cpp" "src/Worlds.cpp" "src/terrain.cpp" "src/noise.cpp" "src/commands.cpp" "src/ThreadedChunkSystem.cpp" "src/TextRenderer.cpp" "src/pch.cpp" "src/biome.cpp" "src/voronoi.cpp" "src/heightfield.cpp" "src/column_cache.cpp")

# Include headers
target_include_directories(OriginBlock PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_precompile_headers(OriginBlock PRIVATE ${PROJECT_SOURCE_DIR}/include/pch.h)


add_library(glfw STATIC IMPORTED)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "C:/GLFW/build/src/Release/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "C:/GLFW/include"
)

# Link libraries
target_link_libraries(OriginBlock
    sfml-system
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
