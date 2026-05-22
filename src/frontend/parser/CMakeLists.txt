narval_collect_sources(PARSER_SOURCES
    EXCLUDE_REGEXES ".*parser\\.test\\.cpp$"
)

add_library(NarvalParser ${PARSER_SOURCES})
target_link_libraries(NarvalParser PUBLIC NarvalProjectIncludes NarvalFrontendUtils)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/parser.test.cpp")
    narval_add_test(NarvalParserTest parser.test.cpp ../module_manager.cpp)
    target_link_libraries(NarvalParserTest PRIVATE NarvalParser)
endif()
