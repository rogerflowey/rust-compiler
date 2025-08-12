# --- Main Compiler Project ---
cmake_minimum_required(VERSION 3.16)
project(MyCompilerProject CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

include_directories(.)
# Enable testing for the entire project.
# This sets the BUILD_TESTING variable to ON, which activates the
# test-building logic in the library's CMakeLists.txt.
enable_testing()

# Add the parsecpp library from the subdirectory.
# CMake will process lib/parsecpp/CMakeLists.txt and create both
# the library target and the test target.
add_subdirectory(lib/parsecpp)

# --- Main Executable ---
add_executable(compiler main.cpp)
target_link_libraries(compiler PRIVATE parsecpp::pratt)

# --- User-friendly Output ---
message(STATUS "Compiler executable will be built as: ${CMAKE_BINARY_DIR}/compiler")
message(STATUS "Test executable will be built as:    ${CMAKE_BINARY_DIR}/libs/parsecpp/parsecpp_tests")
message(STATUS "To run all tests, use 'ctest' in the build directory.")