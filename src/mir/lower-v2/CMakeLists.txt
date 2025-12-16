add_library(mir_lower_v2
    lower_result.cpp
    lower_result.hpp
    lower_internal.hpp
    lower_node.cpp
)

target_link_libraries(mir_lower_v2
    PUBLIC
        mir
        semantic
        type_system
)

target_include_directories(mir_lower_v2
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
