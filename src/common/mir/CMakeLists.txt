# mir_common — Shared MIR definitions and helpers
# Used by opt/mir and eventually the backend.

add_library(mir_common
    utils.cpp
    lower_const.cpp
)

target_include_directories(mir_common
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(mir_common
    PUBLIC
        type
        semantic
)
