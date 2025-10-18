# src/semantic/CMakeLists.txt
file(GLOB_RECURSE SEMANTIC_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(semantic ${SEMANTIC_SOURCES})

target_include_directories(semantic PUBLIC .)
