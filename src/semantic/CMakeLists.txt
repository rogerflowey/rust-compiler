# src/semantic/CMakeLists.txt
file(GLOB_RECURSE SEMANTIC_SOURCES "*.cpp")

add_library(semantic ${SEMANTIC_SOURCES})

target_include_directories(semantic PUBLIC .)
