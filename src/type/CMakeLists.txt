file(GLOB_RECURSE TYPE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(type ${TYPE_SOURCES})

target_include_directories(type
        PUBLIC
                .
                ..
                ../semantic
)
