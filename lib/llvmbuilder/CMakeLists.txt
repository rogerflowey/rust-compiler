add_library(llvmbuilder
    src/builder.cpp
)

target_include_directories(llvmbuilder
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
