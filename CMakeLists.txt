cmake_minimum_required(VERSION 3.20)
project(Narval)
include(GNUInstallDirs)
include(cmake/NarvalSources.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LIB_DIR ${CMAKE_CURRENT_BINARY_DIR}/lib)
file(MAKE_DIRECTORY ${LIB_DIR})
set(LIB_DIR ${LIB_DIR} CACHE INTERNAL "Pasta de arquivos gerados")
set(NARVAL_INSTALL_RUNTIME_DIR "${CMAKE_INSTALL_FULL_LIBDIR}/narval" CACHE PATH "Installed Narval runtime directory")

include_directories(${PROJECT_SOURCE_DIR}/include)

add_library(NarvalProjectIncludes INTERFACE)
target_include_directories(NarvalProjectIncludes INTERFACE ${PROJECT_SOURCE_DIR}/include)

add_library(NarvalFrontendUtils src/frontend/syntax_highlighter.cpp)
target_link_libraries(NarvalFrontendUtils PUBLIC NarvalProjectIncludes)

execute_process(COMMAND llvm-config --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS)
execute_process(COMMAND llvm-config --libs OUTPUT_VARIABLE LLVM_LIBS)
execute_process(COMMAND llvm-config --system-libs OUTPUT_VARIABLE LLVM_SYS_LIBS)
execute_process(COMMAND llvm-config --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS)

string(STRIP "${LLVM_CXX_FLAGS}" LLVM_CXX_FLAGS)
string(STRIP "${LLVM_LIBS}" LLVM_LIBS)
string(STRIP "${LLVM_SYS_LIBS}" LLVM_SYS_LIBS)
string(STRIP "${LLVM_LDFLAGS}" LLVM_LDFLAGS)

set(LLVM_ENABLE_ZSTD OFF)
set(LLVM_ENABLE_TERMINFO OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXX_FLAGS} -fexceptions -frtti -fPIE")

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_library(NarvalCommon INTERFACE)
target_compile_options(NarvalCommon INTERFACE -fexceptions -fPIE)
target_include_directories(NarvalCommon INTERFACE ${LLVM_INCLUDE_DIRS})
target_link_directories(NarvalCommon INTERFACE ${LLVM_LIBRARY_DIRS})
target_compile_definitions(NarvalCommon INTERFACE ${LLVM_DEFINITIONS})
target_link_libraries(NarvalCommon INTERFACE ${LLVM_LIBS} ${LLVM_SYS_LIBS})

add_library(NarvalAllCommon INTERFACE)
target_link_libraries(NarvalAllCommon INTERFACE NarvalProjectIncludes NarvalCommon m)

function(narval_add_test target_name)
    add_executable(${target_name} ${ARGN})
    target_link_libraries(${target_name} PRIVATE NarvalAllCommon NarvalLexer NarvalParser NarvalChecker NarvalCodegen NarvalRuntimeObjs)
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

option(NARVAL_USE_NIR "Enable MLIR-based NIR pipeline (narval dialect + lowering)" OFF)

if(NARVAL_USE_NIR)
    find_package(MLIR REQUIRED CONFIG HINTS /usr/local/lib/cmake/mlir)
    message(STATUS "MLIR found: ${MLIR_DIR}")
    list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")

    if(NOT COMMAND get_subproject_title)
        macro(get_subproject_title result)
            set(${result} "Narval")
        endmacro()
    endif()

    include(AddMLIR)
    include(HandleLLVMOptions)

    include_directories(${MLIR_INCLUDE_DIRS})

    add_subdirectory(src/backend/nir)
endif()

add_executable(NarvalCompiler src/main.cpp src/frontend/module_manager.cpp)
set_target_properties(NarvalCompiler PROPERTIES OUTPUT_NAME narval)
target_link_options(NarvalCompiler PRIVATE -W -fPIE)
target_include_directories(NarvalCompiler PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(NarvalCompiler PRIVATE NarvalAllCommon NarvalLexer NarvalParser NarvalChecker NarvalCodegen NarvalInteractive NarvalRuntimeObjs NarvalRuntimeShared)

if(NARVAL_USE_NIR)
    target_link_libraries(NarvalCompiler PRIVATE NarvalNirDialect)
    target_compile_definitions(NarvalCompiler PRIVATE NARVAL_USE_NIR=1)
    target_include_directories(NarvalCompiler PRIVATE ${CMAKE_BINARY_DIR}/src/backend/nir)

    target_compile_definitions(NarvalCodegen PRIVATE NARVAL_USE_NIR=1)
    target_link_libraries(NarvalCodegen PRIVATE NarvalNirDialect)
    target_include_directories(NarvalCodegen PRIVATE
        ${MLIR_INCLUDE_DIRS}
        ${LLVM_INCLUDE_DIRS}
        ${CMAKE_BINARY_DIR}/src/backend/nir
    )
endif()

add_dependencies(NarvalCompiler NarvalRuntimeO NarvalRuntimeNostdO)
if(NARVAL_BUILD_LSP)
    add_dependencies(NarvalCompiler NarvalLsp)
endif()
if(NARVAL_BUILD_LSP_ALL_PLATFORMS)
    add_dependencies(NarvalCompiler NarvalLspAllPlatforms)
endif()

if(TARGET NarvalCompiler)
    target_compile_definitions(NarvalCompiler PRIVATE
        NARVAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        NARVAL_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include"
        NARVAL_INSTALL_RUNTIME_DIR="${NARVAL_INSTALL_RUNTIME_DIR}"
        NARVAL_DEV_MODE=$<BOOL:${NARVAL_DEV_MODE}>
    )
endif()

install(TARGETS NarvalCompiler
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(TARGET NarvalLsp)
    install(TARGETS NarvalLsp
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(TARGETS NarvalRuntimeShared
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/narval
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/narval
)

install(FILES ${LIB_DIR}/runtime.o
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/narval
    OPTIONAL
)
