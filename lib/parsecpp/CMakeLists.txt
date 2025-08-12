# --- Parsec++ Header-Only Library Definition ---
cmake_minimum_required(VERSION 3.16)
project(parsecpp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- 1. Define the Header-Only Library (Unchanged) ---
# This is what consumers like your 'compiler' executable will use.
add_library(parsec-pratt INTERFACE)
add_library(parsecpp::pratt ALIAS parsec-pratt)

target_include_directories(parsec-pratt INTERFACE
    # The public headers are in the 'include' directory
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- 2. Define Library-Internal Tests ---
# This section will only be processed if testing is enabled in the parent project.
    # Add an executable for our test runner.
    # The source file is in this library's 'src' directory.
    add_executable(parsecpp_tests src/test_parsec.cpp)

    # To compile tests.cpp, it needs to find parsec.hpp and pratt.hpp.
    # We link it against the library target we just defined. This automatically
    # sets up the include paths for the test executable.
    target_link_libraries(parsecpp_tests PRIVATE parsecpp::pratt)

    # Register the executable with CTest.
    # When the user runs 'ctest', this command will be executed.
    add_test(NAME parsecpp-unit-tests COMMAND parsecpp_tests)