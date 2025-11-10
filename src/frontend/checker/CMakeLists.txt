file(GLOB_RECURSE CHECKER_SOURCES "*.cpp")
list(FILTER CHECKER_SOURCES EXCLUDE REGEX ".*checker\\.test\\.cpp$")

add_library(checker ${CHECKER_SOURCES})

include_directories(${PROJECT_SOURCE_DIR}/include)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/checker.test.cpp")
    add_executable(checker_test checker.test.cpp ../module_manager.cpp)
    target_link_libraries(checker_test PRIVATE lexer parser checker)
    include_directories(${PROJECT_SOURCE_DIR}/include)
endif()