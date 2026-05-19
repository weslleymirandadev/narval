cmake_minimum_required(VERSION 3.10)
project(Narval)
include(GNUInstallDirs)
include(cmake/NarvalSources.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# === DEFINIR LIB_DIR NA RAIZ ===
set(LIB_DIR ${CMAKE_CURRENT_BINARY_DIR}/lib)
file(MAKE_DIRECTORY ${LIB_DIR})

# === Passa para subdiretórios ===
set(LIB_DIR ${LIB_DIR} CACHE INTERNAL "Pasta de arquivos gerados")
set(NARVAL_INSTALL_RUNTIME_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/narval" CACHE PATH "Installed Narval runtime directory")

include_directories(${PROJECT_SOURCE_DIR}/include)

add_library(narval_project_includes INTERFACE)
target_include_directories(narval_project_includes INTERFACE ${PROJECT_SOURCE_DIR}/include)

add_library(frontend_utils src/frontend/syntax_highlighter.cpp)
target_link_libraries(frontend_utils PUBLIC narval_project_includes)

execute_process(COMMAND llvm-config --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS)
execute_process(COMMAND llvm-config --libs OUTPUT_VARIABLE LLVM_LIBS)
execute_process(COMMAND llvm-config --system-libs OUTPUT_VARIABLE LLVM_SYS_LIBS)
execute_process(COMMAND llvm-config --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS)

string(STRIP "${LLVM_CXX_FLAGS}" LLVM_CXX_FLAGS)
string(STRIP "${LLVM_LIBS}" LLVM_LIBS)
string(STRIP "${LLVM_SYS_LIBS}" LLVM_SYS_LIBS)
string(STRIP "${LLVM_LDFLAGS}" LLVM_LDFLAGS)

# Fix for zstd dependency issue
set(LLVM_ENABLE_ZSTD OFF)
set(LLVM_ENABLE_TERMINFO OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXX_FLAGS} -fexceptions -frtti -fPIE")

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_library(narval_common INTERFACE)
target_compile_options(narval_common INTERFACE -fexceptions -fPIE)
target_include_directories(narval_common INTERFACE ${LLVM_INCLUDE_DIRS})
target_link_directories(narval_common INTERFACE ${LLVM_LIBRARY_DIRS})
target_compile_definitions(narval_common INTERFACE ${LLVM_DEFINITIONS})
target_link_libraries(narval_common INTERFACE ${LLVM_LIBS} ${LLVM_SYS_LIBS})

add_library(narval_all_common INTERFACE)
target_link_libraries(narval_all_common INTERFACE narval_project_includes narval_common m)

function(narval_add_test target_name)
    add_executable(${target_name} ${ARGN})
    target_link_libraries(${target_name} PRIVATE narval_all_common lexer parser checker generator runtime_objs)
    target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_options(${target_name} PRIVATE -W -fPIE)
endfunction()

add_subdirectory(src/frontend/lexer)
add_subdirectory(src/frontend/parser)
add_subdirectory(src/frontend/checker)
add_subdirectory(src/frontend/interactive)
add_subdirectory(src/backend/runtime)
add_subdirectory(src/backend/codegen)
add_subdirectory(src/lsp)

add_executable(narval src/main.cpp src/frontend/module_manager.cpp)
target_link_options(narval PRIVATE -W -fPIE)
target_include_directories(narval PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(narval PRIVATE narval_all_common lexer parser checker generator interactive runtime_objs runtime_shared)
add_dependencies(narval runtime_o)
if(NARVAL_BUILD_LSP)
    add_dependencies(narval narval-lsp)
endif()
if(NARVAL_BUILD_LSP_ALL_PLATFORMS)
    add_dependencies(narval narval-lsp_all_platforms)
endif()

if(TARGET narval)
    target_compile_definitions(narval PRIVATE
        NARVAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        NARVAL_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include"
        NARVAL_INSTALL_RUNTIME_DIR="${NARVAL_INSTALL_RUNTIME_DIR}"
        NARVAL_DEV_MODE=$<BOOL:${NARVAL_DEV_MODE}>
    )
endif()

install(TARGETS narval
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(TARGET narval-lsp)
    install(TARGETS narval-lsp
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(TARGETS runtime_shared
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/narval
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/narval
)

install(FILES ${LIB_DIR}/runtime.o
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/narval
    OPTIONAL
)
