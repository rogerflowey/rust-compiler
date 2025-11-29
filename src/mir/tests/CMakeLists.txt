if(NOT BUILD_TESTING)
    return()
endif()

set(MIR_TEST_SOURCES
    test_mir_lower.cpp
    test_mir_codegen.cpp
)

foreach(test_source ${MIR_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE mir semantic parser llvmbuilder Catch2::Catch2WithMain)
    catch_discover_tests(${test_name})
endforeach()
