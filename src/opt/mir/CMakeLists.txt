# opt_mir — Optimization MIR (Fixed Skeleton, Floating Flesh)
# Now includes the lowering pass (lower/*.cpp) so this is a real library.

file(GLOB_RECURSE OPT_MIR_SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER OPT_MIR_SOURCES EXCLUDE REGEX "/tests/")

add_library(opt_mir ${OPT_MIR_SOURCES})

target_include_directories(opt_mir
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/passes/rewriters
        ${CMAKE_CURRENT_SOURCE_DIR}/passes/evaluators
)

target_link_libraries(opt_mir
    PUBLIC
        type
    PRIVATE
        semantic
        mir_common
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
