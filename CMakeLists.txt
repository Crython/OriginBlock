# CMakeList.txt : CMake project for VoxelEngine, include source and define
# project specific logic here.
#


cmake_minimum_required(VERSION 3.16)
project(VoxelEngine)

set(CMAKE_CXX_STANDARD 20)

# Enable AddressSanitizer for MSVC (Debug configuration recommended)
if (MSVC)
    # Disable Edit and Continue (incompatible with ASan)
    # Comment out or override the default if present
    string(REPLACE "/ZI" "/Zi" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/ZI" "/Zi" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")

    # Add the ASan flag (applies to compile and link)
    add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
    add_link_options($<$<CONFIG:Debug>:/fsanitize=address>)

    # Optional: Increase stack size for better detection (ASan uses more stack)
    add_link_options($<$<CONFIG:Debug>:/STACK:16777216>)
endif()

# SFML location (adjust to your path)
set(SFML_DIR "C:/SFML-2.6.0/lib/cmake/SFML")

# Find SFML
find_package(SFML 2.6 COMPONENTS system window graphics REQUIRED)
# Find OpenGL
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# Add executable
add_executable(VoxelEngine "src/OriginBlock.cpp" "src/glad/glad.c" "src/shader.cpp" "src/inputs.cpp" "src/textureManager.cpp" "src/texture.cpp" "src/stb_image.cpp" "src/chunk.cpp" "src/Worlds.cpp" "src/terrain.cpp" "src/commands.cpp" "src/ThreadedChunkSystem.cpp" "src/TextRenderer.cpp")

# Include headers
target_include_directories(VoxelEngine PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_library(glfw STATIC IMPORTED)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "C:/GLFW/build/src/Release/glfw3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "C:/GLFW/include"
)

# Link libraries
target_link_libraries(VoxelEngine
    sfml-system
    OpenGL::GL
    glfw
    Threads::Threads
)

add_custom_command(TARGET VoxelEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:VoxelEngine>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/res
        $<TARGET_FILE_DIR:VoxelEngine>/res
)


# TODO: Add tests and install targets if needed.
