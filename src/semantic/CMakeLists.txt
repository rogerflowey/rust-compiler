# src/semantic/CMakeLists.txt
file(GLOB_RECURSE SEMANTIC_SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SEMANTIC_SOURCES EXCLUDE REGEX "/tests/")

add_library(semantic ${SEMANTIC_SOURCES})

target_include_directories(semantic PUBLIC .)

if(BUILD_TESTING)
	add_subdirectory(tests)
endif()
