# Tankshooter — CMake build for Windows, macOS, and Linux.
#
#   cmake -B build
#   cmake --build build
#
# raylib is fetched and built automatically on first configure, so no
# manual dependency setup is needed on Windows or macOS.

cmake_minimum_required(VERSION 3.15)

project(Tankshooter
    VERSION 0.1.0
    DESCRIPTION "Cover-based tank shooter prototype (raylib)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Warnings: keep the prototype honest on every compiler.
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# --- raylib (fetched + built automatically) -----------------------------------
include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)  # static raylib on every platform

FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
)
FetchContent_MakeAvailable(raylib)

# --- nlohmann/json (header-only, for Tanks.json runtime config) -------------------
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(nlohmann_json)

# --- the game ------------------------------------------------------------------
add_executable(tankshooter tankshooter.cpp)
target_link_libraries(tankshooter PRIVATE raylib nlohmann_json::nlohmann_json)

# Drop the binary at the build-tree root so it is easy to find.
set_target_properties(tankshooter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Platform libraries (winmm/gdi32 on Windows, Cocoa/IOKit/OpenGL frameworks
# on macOS, X11/GL on Linux) are all handled by raylib's own CMake targets,
# so nothing extra is needed here.
