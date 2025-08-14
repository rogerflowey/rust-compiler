# --- Parsec++ Header-Only Library Definition ---
cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- 1. Define the Header-Only Library ---
# This is what consumers like your 'compiler' executable will use.
add_library(parsecpp INTERFACE)

target_include_directories(parsecpp INTERFACE
    # The public headers are in the 'include' directory
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(BUILD_TESTING)
    include(FetchContent)
FetchContent_Declare(
  googletest
    URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(parsecpp_tests src/test_parsec_basic.cpp)

    target_link_libraries(parsecpp_tests PRIVATE
        parsecpp
        gtest_main
    )
    include(CTest)
    add_test(NAME parsecpp-basic COMMAND parsecpp_tests)

endif()