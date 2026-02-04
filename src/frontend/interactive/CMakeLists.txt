file(GLOB_RECURSE INTERACTIVE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(interactive ${INTERACTIVE_SOURCES})

include_directories(${PROJECT_SOURCE_DIR}/include)

# Use LLVM configuration from root CMakeLists.txt instead of find_package
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Link with LLVM libraries using llvm-config output
target_link_libraries(interactive PRIVATE 
    narval_all_common 
    checker 
    generator
    ${LLVM_LIBS}
    ${LLVM_SYS_LIBS}
)
