file(GLOB_RECURSE IR3_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(ir3 ${IR3_SOURCES})

target_include_directories(ir3 PUBLIC ..)
target_link_libraries(ir3 PUBLIC semantic)
