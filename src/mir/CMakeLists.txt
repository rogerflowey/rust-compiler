file(GLOB_RECURSE MIR_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(mir ${MIR_SOURCES})

target_include_directories(mir
	PUBLIC
		.
		..
		../semantic
)
