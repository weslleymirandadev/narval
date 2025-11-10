file(GLOB_RECURSE PARSER_SOURCES "*.cpp")
list(FILTER PARSER_SOURCES EXCLUDE REGEX ".*parser\\.test\\.cpp$")

add_library(parser ${PARSER_SOURCES})
target_link_libraries(parser PUBLIC ruphi_project_includes)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/parser.test.cpp")
    ruphi_add_test(parser_test parser.test.cpp ../module_manager.cpp)
endif()