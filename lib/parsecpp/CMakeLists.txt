# --- Parsec++ Header-Only Library Definition ---
cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- 1. Define the Header-Only Library ---
add_library(parsecpp INTERFACE)

target_include_directories(parsecpp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- 2. Define Tests for the Library ---
if(BUILD_TESTING)
    # GTest is now fetched by the parent project. We just use it.
    
    add_executable(parsecpp_tests test/test_parsec_basic.cpp)
    add_executable(parsecpp_pratt_tests test/test_pratt.cpp)
    add_executable(parsecpp_pratt_move_only_tests test/test_pratt_move_only.cpp)

    # Link against the library it's testing and GTest.
    target_link_libraries(parsecpp_tests PRIVATE
        parsecpp
        gtest_main
    )
    target_link_libraries(parsecpp_pratt_tests PRIVATE
        parsecpp
        gtest_main
    )
    target_link_libraries(parsecpp_pratt_move_only_tests PRIVATE
        parsecpp
        gtest_main
    )
    
    # Register with CTest.
    include(CTest)
    add_test(NAME parsecpp-basic COMMAND parsecpp_tests)
    add_test(NAME parsecpp-pratt COMMAND parsecpp_pratt_tests)
    add_test(NAME parsecpp-pratt-move-only COMMAND parsecpp_pratt_move_only_tests)
endif()