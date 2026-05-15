# cmd/CMakeLists.txt
# Build the semantic pipeline executable

add_executable(semantic_pipeline semantic_pipeline.cpp)

# Set include directories
target_include_directories(semantic_pipeline PRIVATE .. src)

# Link against required libraries
target_link_libraries(semantic_pipeline PRIVATE 
    parser 
    semantic
)

# Set C++ standard
set_property(TARGET semantic_pipeline PROPERTY CXX_STANDARD 23)
set_property(TARGET semantic_pipeline PROPERTY CXX_STANDARD_REQUIRED ON)

# Add compiler flags
target_compile_options(semantic_pipeline PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ir3_pipeline ir3_pipeline.cpp)

target_include_directories(ir3_pipeline PRIVATE .. src)

target_link_libraries(ir3_pipeline PRIVATE
    parser
    semantic
    ir3
)

set_property(TARGET ir3_pipeline PROPERTY CXX_STANDARD 23)
set_property(TARGET ir3_pipeline PROPERTY CXX_STANDARD_REQUIRED ON)

target_compile_options(ir3_pipeline PRIVATE -Wall -Wextra -Wpedantic)
