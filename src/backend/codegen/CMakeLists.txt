file(GLOB_RECURSE GENERATOR_SOURCES "*.cpp")
list(FILTER GENERATOR_SOURCES EXCLUDE REGEX ".*generator\\.test\\.cpp$")

add_library(generator ${GENERATOR_SOURCES})
target_link_libraries(generator PUBLIC narval_project_includes narval_all_common)


if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generator.test.cpp")
    narval_add_test(generator_test generator.test.cpp ../../frontend/module_manager.cpp)
    target_link_libraries(generator_test PRIVATE generator)
    
    if(TARGET generator_test)
        target_compile_definitions(generator_test PRIVATE 
            NARVAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
            NARVAL_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include"
        )
    endif()
endif()