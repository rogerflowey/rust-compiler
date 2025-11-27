if(NOT BUILD_TESTING)
    return()
endif()

set(LEXER_TEST_SOURCES
    test_lexer.cpp
)

foreach(test_source ${LEXER_TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE Catch2::Catch2WithMain)
    catch_discover_tests(${test_name})
endforeach()
