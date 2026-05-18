file(GLOB_RECURSE RISCV_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(riscv ${RISCV_SOURCES})

target_include_directories(riscv PUBLIC ..)
target_link_libraries(riscv PUBLIC semantic ir3)
