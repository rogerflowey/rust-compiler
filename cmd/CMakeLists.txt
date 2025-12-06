# cmd/CMakeLists.txt
# Build the semantic pipeline executable

add_executable(semantic_pipeline semantic_pipeline.cpp)

# Build the IR pipeline executable
add_executable(ir_pipeline ir_pipeline.cpp)

# Set include directories
target_include_directories(semantic_pipeline PRIVATE .. src)
target_include_directories(ir_pipeline PRIVATE .. src)

# Link against required libraries
target_link_libraries(semantic_pipeline PRIVATE 
    parser 
    semantic
)

target_link_libraries(ir_pipeline PRIVATE
    parser
    semantic
    mir
)

# Set C++ standard
set_property(TARGET semantic_pipeline PROPERTY CXX_STANDARD 23)
set_property(TARGET semantic_pipeline PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET ir_pipeline PROPERTY CXX_STANDARD 23)
set_property(TARGET ir_pipeline PROPERTY CXX_STANDARD_REQUIRED ON)

# Add compiler flags
target_compile_options(semantic_pipeline PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(ir_pipeline PRIVATE -Wall -Wextra -Wpedantic)