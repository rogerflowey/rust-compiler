if(NOT BUILD_TESTING)
    return()
endif()

set(SEMANTIC_TEST_SOURCES
    test_const_type_check.cpp
    test_control_flow_linking.cpp
    test_exit_check.cpp
    test_expr_check.cpp
    test_expr_check_advanced.cpp
    test_expr_check_control_flow.cpp
    test_hir_converter.cpp
    test_name_resolution.cpp
    test_temp_ref_desugaring.cpp
    test_trait_check.cpp
    test_type_compatibility.cpp
    test_type_const.cpp
)

foreach(test_source ${SEMANTIC_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${test_name} PRIVATE semantic parser mir llvmbuilder Catch2::Catch2WithMain)
    catch_discover_tests(${test_name})
endforeach()
