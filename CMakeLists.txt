# --- Main Compiler Project ---
cmake_minimum_required(VERSION 3.28)
project(RCompiler CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif()


# Include source and header directories.
# The "." allows includes like "src/parser/..." and "src/parser/tests/..."
# NOTE: While this works, using target_include_directories is the modern approach.
# We will rely on that for our new library.
include_directories(. src)

# --- Testing Setup ---
# Enable testing for the entire project.
enable_testing()

# Fetch Catch2 so subprojects can reuse the same testing infrastructure.
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.11.0
    )
    FetchContent_MakeAvailable(catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
endif()


# --- Dependencies ---
add_subdirectory(lib/llvmbuilder)
add_subdirectory(lib/parsecpp)
add_subdirectory(src/span)
add_subdirectory(src/lexer)

# --- Project Modules ---
# Add our new parser library. This will execute src/parser/CMakeLists.txt
# and create the 'parser' target.
add_subdirectory(src/parser)
add_subdirectory(src/semantic)
add_subdirectory(src/mir)
add_subdirectory(cmd)


# --- Main Executable ---
add_executable(compiler main.cpp)
target_include_directories(compiler PRIVATE src)

# Link the main executable against our new parser library.
# Because 'parser' has a PUBLIC dependency on 'parsecpp', we no longer
# need to link parsecpp directly here.
target_link_libraries(compiler PRIVATE parser semantic mir)

# --- User-friendly Output ---
message(STATUS "Compiler executable will be built as:  ${CMAKE_BINARY_DIR}/compiler")
if(BUILD_TESTING)
    message(STATUS "To run all tests, use 'ctest' in the build directory.")
endif()
