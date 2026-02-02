file(GLOB_RECURSE INTERACTIVE_SOURCES "*.cpp")

add_library(interactive ${INTERACTIVE_SOURCES})

include_directories(${PROJECT_SOURCE_DIR}/include)

target_link_libraries(interactive PRIVATE 
    narval_all_common 
    checker 
    generator
)
