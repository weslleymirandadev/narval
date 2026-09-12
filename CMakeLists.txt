cmake_minimum_required(VERSION 3.20)
project(Narval)
include(CTest)
include(GNUInstallDirs)
include(cmake/NarvalSources.cmake)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

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
    target_link_libraries(${target_name} PRIVATE NarvalAllCommon NarvalLexer NarvalParser NarvalChecker NarvalRuntimeObjs NarvalNirDialect)
    target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_options(${target_name} PRIVATE -W -fPIE)
endfunction()

add_subdirectory(src/frontend/lexer)
add_subdirectory(src/frontend/parser)
add_subdirectory(src/frontend/checker)
add_subdirectory(src/frontend/interactive)
add_subdirectory(src/backend/runtime)

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

# The language server is built on demand: `make NarvalLsp`. Turning this ON makes a plain
# `make` / `make Narval` build it too (it was ON in the cache, which is why every build
# spent minutes linking it).
option(NARVAL_BUILD_LSP "Also build the language server with the compiler" OFF)
add_subdirectory(src/lsp)

# ── Runtime embedded in the compiler ─────────────────────────────────────────
# The two runtime objects become byte arrays inside the narval binary, so linking a
# program never depends on build/lib/*.o or NARVAL_HOME being present. This has to
# live here: add_custom_command is scoped to the directory of the target that
# consumes its OUTPUT.
set(NARVAL_RUNTIME_BLOB_SRC ${CMAKE_BINARY_DIR}/generated/narval_runtime_blobs.cpp)
add_custom_command(
    OUTPUT ${NARVAL_RUNTIME_BLOB_SRC}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND sh -c "printf '#include <cstddef>\\n' > ${NARVAL_RUNTIME_BLOB_SRC}"
    COMMAND sh -c "xxd -i -n narval_runtime_obj ${LIB_DIR}/runtime.o >> ${NARVAL_RUNTIME_BLOB_SRC}"
    COMMAND sh -c "xxd -i -n narval_runtime_nostd_obj ${LIB_DIR}/runtime_nostd.o >> ${NARVAL_RUNTIME_BLOB_SRC}"
    # Depend on the runtime OBJECT FILES, not only on the object libraries: the blob is
    # regenerated exactly when runtime.o / runtime_nostd.o change. Depending on the
    # custom targets instead would re-run this (2 MB of xxd + a recompile) every build.
    DEPENDS ${LIB_DIR}/runtime.o ${LIB_DIR}/runtime_nostd.o
    COMMENT "Embedding runtime objects into the compiler"
    VERBATIM
)
set_source_files_properties(${NARVAL_RUNTIME_BLOB_SRC} PROPERTIES GENERATED TRUE)

add_executable(Narval src/main.cpp src/frontend/module_manager.cpp
    ${NARVAL_RUNTIME_BLOB_SRC})   # runtime embedded in the compiler (see src/backend/runtime)

# The REPL JITs the REPL program and resolves runtime symbols (nv_box_*, the
# nv_sqlite_* primitives, ...) from the running process, so the narval binary has
# to export them; without this only libc/libm symbols are visible and every REPL
# call into the runtime fails with "Symbols not found".
set_target_properties(Narval PROPERTIES ENABLE_EXPORTS ON)
set_target_properties(Narval PROPERTIES OUTPUT_NAME narval)
target_link_options(Narval PRIVATE -W -fPIE)
target_include_directories(Narval PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(Narval PRIVATE NarvalAllCommon NarvalLexer NarvalParser NarvalChecker NarvalInteractive NarvalRuntimeObjs NarvalRuntimeShared NarvalNirDialect)
# The runtime .o files have to be produced before the blob is generated, and building
# just the Narval target would otherwise leave them stale (the embedded copy is what
# the linker uses, so a stale blob silently links an old runtime).
add_dependencies(Narval NarvalRuntimeO NarvalRuntimeNostdO)
target_compile_definitions(Narval PRIVATE NARVAL_USE_NIR=1)
target_include_directories(Narval PRIVATE ${CMAKE_BINARY_DIR}/src/backend/nir)

add_dependencies(Narval NarvalRuntimeO NarvalRuntimeNostdO)
if(NARVAL_BUILD_LSP)
    add_dependencies(Narval NarvalLsp)
endif()
if(NARVAL_BUILD_LSP_ALL_PLATFORMS)
    add_dependencies(Narval NarvalLspAllPlatforms)
endif()

if(TARGET Narval)
    target_compile_definitions(Narval PRIVATE
        NARVAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        NARVAL_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include"
        NARVAL_INSTALL_RUNTIME_DIR="${NARVAL_INSTALL_RUNTIME_DIR}"
        NARVAL_DEV_MODE=$<BOOL:${NARVAL_DEV_MODE}>
    )
endif()

if(BUILD_TESTING)
    # End-to-end integration suite: one .nv program per case in tests/cases/, each with
    # its expected stdout next to it, driven by tests/run.sh (bash, no Python — the
    # runner is a loop over the directory).
    add_test(NAME narval_integration
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/run.sh
                     $<TARGET_FILE:Narval>)
    set_tests_properties(narval_integration PROPERTIES TIMEOUT 300)
endif()

install(TARGETS Narval
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
