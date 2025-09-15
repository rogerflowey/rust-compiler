# --- Main Compiler Project ---
cmake_minimum_required(VERSION 3.16)
project(RCompiler CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

set(CMAKE_CXX_COMPILER_LAUNCHER ccache)


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
target_include_directories(compiler PRIVATE src)
target_link_libraries(compiler PRIVATE parsecpp)


if(BUILD_TESTING)
    add_library(compiler_tests_properties INTERFACE)

    # This PCH will contain headers needed for testing, but not part of parsecpp
    target_precompile_headers(compiler_tests_properties INTERFACE
        <gtest/gtest.h>
    )

    # 3. Find all your test source files
    file(GLOB COMPILER_TEST_SOURCES "test/parser/*.cpp" "test/lexer/*.cpp")

    # 4. Loop over each test source file
    foreach(test_source_file ${COMPILER_TEST_SOURCES})
        get_filename_component(test_name ${test_source_file} NAME_WE)

        add_executable(${test_name} ${test_source_file})

        # Set include directories for this specific test
        target_include_directories(${test_name} PRIVATE src)

        # Link against the necessary libraries AND our PCH interface library
        target_link_libraries(${test_name} PRIVATE
            parsecpp
            gtest_main
            compiler_tests_properties  # <-- This is the key part!
        )

        add_test(NAME ${test_name} COMMAND ${test_name})
    endforeach()
endif()


# --- User-friendly Output ---
message(STATUS "Compiler executable will be built as:  ${CMAKE_BINARY_DIR}/compiler")
if(BUILD_TESTING)
    message(STATUS "Parsec lib tests will be built as:   ${CMAKE_BINARY_DIR}/lib/parsecpp/parsecpp_tests")
    message(STATUS "Compiler tests will be built as:     ${CMAKE_BINARY_DIR}/compiler_tests")
    message(STATUS "To run all tests, use 'ctest' in the build directory.")
endif()