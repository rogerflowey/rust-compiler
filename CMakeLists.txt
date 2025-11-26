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
# The "." allows includes like "src/parser/..." and "test/parser/..."
# NOTE: While this works, using target_include_directories is the modern approach.
# We will rely on that for our new library.
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
add_subdirectory(lib/llvmbuilder)
add_subdirectory(lib/parsecpp)
add_subdirectory(src/span)

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


if(BUILD_TESTING)
    add_library(compiler_tests_properties INTERFACE)

    target_precompile_headers(compiler_tests_properties INTERFACE
        <gtest/gtest.h>
    )

    # 3. Find all your test source files
    file(GLOB_RECURSE COMPILER_TEST_SOURCES "test/*.cpp")

    # 4. Loop over each test source file
    foreach(test_source_file ${COMPILER_TEST_SOURCES})
        get_filename_component(test_name ${test_source_file} NAME_WE)

        # Special handling for HIR converter test - add the converter source
        add_executable(${test_name} ${test_source_file})
        
        target_include_directories(${test_name} PRIVATE src)

        # Link tests against the parser library.
        target_link_libraries(${test_name} PRIVATE
            parser
            semantic
            mir
            llvmbuilder
            gtest_main
            compiler_tests_properties
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
