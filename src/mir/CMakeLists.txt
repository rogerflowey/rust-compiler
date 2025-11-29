file(GLOB_RECURSE MIR_SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER MIR_SOURCES EXCLUDE REGEX "/tests/")

add_library(mir ${MIR_SOURCES})

target_include_directories(mir
	PUBLIC
		.
		..
		../semantic
)

target_link_libraries(mir
	PUBLIC
		llvmbuilder
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
