narval_collect_sources(CHECKER_SOURCES
    EXCLUDE_REGEXES ".*checker\\.test\\.cpp$"
)

add_library(checker ${CHECKER_SOURCES})
target_sources(checker PRIVATE ../attributes/attribute_mapper.cpp)
target_link_libraries(checker PUBLIC narval_project_includes frontend_utils)

include_directories(${PROJECT_SOURCE_DIR}/include)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/checker.test.cpp")
    narval_add_test(checker_test checker.test.cpp ../module_manager.cpp)
    target_link_libraries(checker_test PRIVATE checker)
endif()
