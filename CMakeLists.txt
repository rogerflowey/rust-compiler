# --- Main Compiler Project ---
cmake_minimum_required(VERSION 3.16)
project(MyCompilerProject CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# Include source and header directories.
# The "." allows includes like "src/parser/..." and "test/parser/..."
include_directories(. src)

# --- Testing Setup ---
# Enable testing for the entire project.
enable_testing()

# Fetch GoogleTest at the top level so all subprojects and tests can use it.
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
      googletest
        URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()


# --- Dependencies ---
# Add the parsecpp library from the subdirectory.
# Its CMakeLists.txt will create the 'parsecpp' library target
# and, if testing is enabled, the 'parsecpp_tests' target.
add_subdirectory(lib/parsecpp)


# --- Main Executable ---
add_executable(compiler main.cpp)
target_link_libraries(compiler PRIVATE parsecpp)


# --- Compiler-Specific Tests ---
if(BUILD_TESTING)
    # Add an executable for the compiler's own unit tests.
    add_executable(compiler_tests
        test/parser/test_pattern_parser.cpp
        test/lexer/test_lexer.cpp
    test/parser/test_type_parser.cpp
        # Add other compiler test files here as you create them
    )

    # Link the tests against the libraries they need:
    # - parsecpp: because PatternGrammar uses it.
    # - gtest_main: to get the main() function and GTest framework.
    target_link_libraries(compiler_tests PRIVATE
        parsecpp
        gtest_main
    )

    # Register this executable with CTest.
    add_test(NAME compiler-pattern-grammar COMMAND compiler_tests)
endif()


# --- User-friendly Output ---
message(STATUS "Compiler executable will be built as:  ${CMAKE_BINARY_DIR}/compiler")
if(BUILD_TESTING)
    message(STATUS "Parsec lib tests will be built as:   ${CMAKE_BINARY_DIR}/lib/parsecpp/parsecpp_tests")
    message(STATUS "Compiler tests will be built as:     ${CMAKE_BINARY_DIR}/compiler_tests")
    message(STATUS "To run all tests, use 'ctest' in the build directory.")
endif()