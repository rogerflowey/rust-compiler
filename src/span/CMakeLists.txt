file(GLOB SPAN_SOURCES "*.cpp")

add_library(span STATIC ${SPAN_SOURCES})

target_include_directories(span PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
