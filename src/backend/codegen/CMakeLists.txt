narval_collect_sources(GENERATOR_SOURCES
    EXCLUDE_REGEXES ".*generator\\.test\\.cpp$"
)

add_library(NarvalCodegen ${GENERATOR_SOURCES})
target_link_libraries(NarvalCodegen PUBLIC NarvalProjectIncludes NarvalAllCommon)


if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generator.test.cpp")
    narval_add_test(NarvalCodegenTest generator.test.cpp ../../frontend/module_manager.cpp)
    target_link_libraries(NarvalCodegenTest PRIVATE NarvalCodegen)

    if(TARGET NarvalCodegenTest)
        target_compile_definitions(NarvalCodegenTest PRIVATE
            NARVAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
            NARVAL_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include"
        )
    endif()
endif()
