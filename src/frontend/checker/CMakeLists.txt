narval_collect_sources(CHECKER_SOURCES
    EXCLUDE_REGEXES ".*checker\\.test\\.cpp$"
)

add_library(NarvalChecker ${CHECKER_SOURCES})
target_sources(NarvalChecker PRIVATE ../attributes/attribute_mapper.cpp)
target_link_libraries(NarvalChecker PUBLIC NarvalProjectIncludes NarvalFrontendUtils)

include_directories(${PROJECT_SOURCE_DIR}/include)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/checker.test.cpp")
    narval_add_test(NarvalCheckerTest checker.test.cpp ../module_manager.cpp)
    target_link_libraries(NarvalCheckerTest PRIVATE NarvalChecker)
endif()
