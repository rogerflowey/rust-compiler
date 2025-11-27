if(NOT BUILD_TESTING)
    return()
endif()

set(PARSER_TEST_SOURCES
    test_expr_parser.cpp
    test_item_parser.cpp
    test_pattern_parser.cpp
    test_stmt_parser.cpp
    test_type_parser.cpp
)

foreach(test_source ${PARSER_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE parser Catch2::Catch2WithMain)
    catch_discover_tests(${test_name})
endforeach()
