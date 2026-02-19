if(NOT BUILD_TESTING)
    return()
endif()

add_executable(test_opt_mir test_opt_mir.cpp test_lower.cpp test_region_facts.cpp test_type_constraints.cpp test_lowering_params.cpp test_place_rewrite.cpp)
target_include_directories(test_opt_mir PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_opt_mir PRIVATE opt_mir type semantic mir_common Catch2::Catch2WithMain)
catch_discover_tests(test_opt_mir)
