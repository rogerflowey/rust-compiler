
cmake_minimum_required(VERSION 3.28)

# --- 1. Define the Header-Only Library ---
add_library(parsecpp INTERFACE)

# Tell consumers where to find the headers
target_include_directories(parsecpp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_precompile_headers(parsecpp INTERFACE
    "include/parsec.hpp"
)

# --- 2. Define Tests for the Library ---
if(BUILD_TESTING)
    set(PARSECPP_TEST_SOURCES
        test_parsec_basic.cpp
        test_pratt.cpp
        test_pratt_move_only.cpp
    )

    foreach(test_source ${PARSECPP_TEST_SOURCES})
        get_filename_component(test_name ${test_source} NAME_WE)
        set(target_name parsecpp_${test_name})
        add_executable(${target_name} tests/${test_source})
        target_link_libraries(${target_name} PRIVATE parsecpp Catch2::Catch2WithMain)
        catch_discover_tests(${target_name})
    endforeach()
endif()