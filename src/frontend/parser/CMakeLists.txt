narval_collect_sources(PARSER_SOURCES
    EXCLUDE_REGEXES ".*parser\\.test\\.cpp$"
)

add_library(parser ${PARSER_SOURCES})
target_link_libraries(parser PUBLIC narval_project_includes)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/parser.test.cpp")
    narval_add_test(parser_test parser.test.cpp ../module_manager.cpp)
    target_link_libraries(parser_test PRIVATE parser)
endif()
