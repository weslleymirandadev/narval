cmake_minimum_required(VERSION 3.20)
project(Narval VERSION 1.0.0)
include(CTest)
include(GNUInstallDirs)
include(cmake/NarvalSources.cmake)

# The version the compiler reports, in the help line, the REPL/notebook prompts
# (include/frontend/version.hpp) and the directory the embedded assets are materialised into
# (src/frontend/narval_home.cpp). A release passes its git tag: -DNARVAL_VERSION=<tag>.
#
# Deliberately not a cache entry: a cached default would freeze an older version in a build tree
# that is reconfigured after a version bump, and the compiler would keep reporting it.
if(NOT DEFINED NARVAL_VERSION)
    set(NARVAL_VERSION "${PROJECT_VERSION}")
endif()

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
# Paths every target needs: the REPL looks for the runtime relative to them and the module
# manager for stdlib/. They used to be defined on the Narval target alone, so
# NarvalInteractive fell back to the hardcoded POSIX path in repl_state.hpp.
target_compile_definitions(NarvalProjectIncludes INTERFACE
    NARVAL_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    NARVAL_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include"
    NARVAL_INSTALL_RUNTIME_DIR="${NARVAL_INSTALL_RUNTIME_DIR}"
)

add_library(NarvalFrontendUtils src/frontend/syntax_highlighter.cpp)
target_link_libraries(NarvalFrontendUtils PUBLIC NarvalProjectIncludes)

# The AST nodes' destructors — the vtable key functions — are defined here, in the front-end,
# so the language server can link without the NIR/MLIR back-end (src/lsp/ast_codegen_stubs.cpp
# is the other half of that). It is a library of its own because of link ORDER: the vtables
# reference every nir_codegen() implementation, so ld has to read the users (parser/checker),
# then these anchors, then NarvalNirDialect, which defines those functions. Inside
# NarvalFrontendUtils.a the anchors were scanned before their users and dropped.
add_library(NarvalAstAnchors STATIC src/frontend/ast_node_anchors.cpp)
target_link_libraries(NarvalAstAnchors PUBLIC NarvalProjectIncludes)

# Module resolution is an archive of its own: it is the front-end's module/stdlib search and the
# language server links it too (it used to be a source of the compiler executable only, so the
# server could not resolve a single stdlib import).
add_library(NarvalModuleManager STATIC src/frontend/module_manager.cpp)
target_link_libraries(NarvalModuleManager PUBLIC NarvalProjectIncludes)

# Three archives are mutually dependent: the AST vtables (NarvalAstAnchors) reference every
# nir_codegen() implementation (NarvalNirDialect) and the codegen objects reference the dtors
# again. ld scans an archive once, left to right, so they need a rescanning group — kept to
# those three on purpose: naming a library whose own dependencies reach MLIR/LLVM makes the
# group rescan those too, and ld then reports "multiple definition" for LLVM (writeToOutput)
# because it can pull the same member out of two copies of the same archive.
# NarvalModuleManager is linked BEFORE the lexer: module resolution references it, and a
# definition may come later than its use but never earlier.
# MSVC's linker resolves across libraries and does not implement the feature.
if(MSVC)
    set(NARVAL_AST_LINK_GROUP NarvalAstAnchors NarvalNirDialect NarvalFrontendUtils)
else()
    set(NARVAL_AST_LINK_GROUP "$<LINK_GROUP:RESCAN,NarvalAstAnchors,NarvalNirDialect,NarvalFrontendUtils>")
endif()

execute_process(COMMAND llvm-config --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS)
execute_process(COMMAND llvm-config --libs OUTPUT_VARIABLE LLVM_LIBS)
execute_process(COMMAND llvm-config --system-libs OUTPUT_VARIABLE LLVM_SYS_LIBS)
execute_process(COMMAND llvm-config --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS)
# Where those libraries are: the -lLLVM* names alone only resolve when LLVM sits in a directory
# the linker searches by default, which is the case for this machine's /usr/local/lib. Against an
# LLVM anywhere else (a release tarball, a second prefix) every one of them fails to link.
execute_process(COMMAND llvm-config --libdir OUTPUT_VARIABLE LLVM_LIBRARY_DIRS)

string(STRIP "${LLVM_CXX_FLAGS}" LLVM_CXX_FLAGS)
string(STRIP "${LLVM_LIBS}" LLVM_LIBS)
string(STRIP "${LLVM_SYS_LIBS}" LLVM_SYS_LIBS)
string(STRIP "${LLVM_LDFLAGS}" LLVM_LDFLAGS)
string(STRIP "${LLVM_LIBRARY_DIRS}" LLVM_LIBRARY_DIRS)

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
# libm is a separate library on POSIX only; the MSVC CRT already carries the math.
if(MSVC)
    target_link_libraries(NarvalAllCommon INTERFACE NarvalProjectIncludes NarvalCommon)
else()
    target_link_libraries(NarvalAllCommon INTERFACE NarvalProjectIncludes NarvalCommon m)
endif()

function(narval_add_test target_name)
    # Not built on Windows: the frontend tests link the runtime, and there the runtime is a
    # MinGW relocatable object (cl.exe cannot even compile the POSIX C, so MSVC cannot link
    # it). EXCLUDE_FROM_ALL keeps them out of the default build, which is how the Windows
    # build stays limited to narval.exe and narval-lsp.exe (see scripts/); asking for one of
    # them explicitly is what would fail.
    set(_exclude_from_all)
    if(MSVC)
        message(STATUS "Test ${target_name}: EXCLUDE_FROM_ALL on Windows (MinGW runtime object)")
        set(_exclude_from_all EXCLUDE_FROM_ALL)
    endif()
    add_executable(${target_name} ${_exclude_from_all} ${ARGN})
    target_link_libraries(${target_name} PRIVATE NarvalAllCommon NarvalModuleManager
        NarvalLexer NarvalParser NarvalChecker NarvalRuntimeObjs ${NARVAL_AST_LINK_GROUP})
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
# The no_std runtime is POSIX-syscall based: it is only built where it can run, and the
# compiler refuses @[no_std] on Windows (src/main.cpp), so its blob stays empty there.
set(NARVAL_RUNTIME_BLOB_DEPS ${LIB_DIR}/runtime.o)
if(NOT MSVC)
    list(APPEND NARVAL_RUNTIME_BLOB_DEPS ${LIB_DIR}/runtime_nostd.o)
endif()
# The standard library travels the same way: the embedded modules are what the compiler reads
# when no stdlib/ directory sits next to the binary (src/frontend/narval_home.cpp writes them
# out on first use, src/frontend/module_manager.cpp looks them up there).
file(GLOB NARVAL_STDLIB_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/stdlib/*.nv)
set(NARVAL_STDLIB_NAMES "")
foreach(stdlib_file IN LISTS NARVAL_STDLIB_SOURCES)
    get_filename_component(stdlib_name ${stdlib_file} NAME)
    list(APPEND NARVAL_STDLIB_NAMES ${stdlib_name})
endforeach()
list(APPEND NARVAL_RUNTIME_BLOB_DEPS ${NARVAL_STDLIB_SOURCES})
add_custom_command(
    OUTPUT ${NARVAL_RUNTIME_BLOB_SRC}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    # No sh/xxd: the Windows build has neither (see cmake/NarvalEmbedBlob.cmake).
    COMMAND ${CMAKE_COMMAND}
            "-DOUT=${NARVAL_RUNTIME_BLOB_SRC}"
            "-DSTD_OBJ=${LIB_DIR}/runtime.o"
            "-DNOSTD_OBJ=${LIB_DIR}/runtime_nostd.o"
            "-DSTDLIB_DIR=${PROJECT_SOURCE_DIR}/stdlib"
            "-DSTDLIB_FILES=${NARVAL_STDLIB_NAMES}"
            -P ${PROJECT_SOURCE_DIR}/cmake/NarvalEmbedBlob.cmake
    # Depend on the runtime OBJECT FILES, not only on the object libraries: the blob is
    # regenerated exactly when runtime.o / runtime_nostd.o change. Depending on the
    # custom targets instead would re-run this (2 MB of xxd + a recompile) every build.
    DEPENDS ${NARVAL_RUNTIME_BLOB_DEPS} ${PROJECT_SOURCE_DIR}/cmake/NarvalEmbedBlob.cmake
    COMMENT "Embedding runtime objects and stdlib into the compiler"
    VERBATIM
)
set_source_files_properties(${NARVAL_RUNTIME_BLOB_SRC} PROPERTIES GENERATED TRUE)

add_executable(Narval src/main.cpp
    src/frontend/narval_home.cpp            # first run materialises the embedded assets
    ${NARVAL_RUNTIME_BLOB_SRC})   # runtime embedded in the compiler (see src/backend/runtime)

# The REPL JITs the REPL program and resolves runtime symbols (nv_box_*, the
# nv_sqlite_* primitives, ...) from the running process, so the narval binary has
# to export them; without this only libc/libm symbols are visible and every REPL
# call into the runtime fails with "Symbols not found".
set_target_properties(Narval PROPERTIES ENABLE_EXPORTS ON)
set_target_properties(Narval PROPERTIES OUTPUT_NAME narval)
target_link_options(Narval PRIVATE -W -fPIE)
target_include_directories(Narval PRIVATE ${PROJECT_SOURCE_DIR}/include)
# On Windows the runtime is a MinGW object that travels EMBEDDED in narval.exe (the compiler
# writes it out and links the program with gcc), so none of it is linked into the compiler
# executable. Accepted consequence: the REPL/JIT cannot resolve the nv_* symbols there.
if(MSVC)
    target_link_libraries(Narval PRIVATE NarvalAllCommon NarvalModuleManager
        NarvalLexer NarvalParser NarvalChecker NarvalInteractive ${NARVAL_AST_LINK_GROUP})
else()
    target_link_libraries(Narval PRIVATE NarvalAllCommon NarvalModuleManager
        NarvalLexer NarvalParser NarvalChecker NarvalInteractive NarvalRuntimeObjs NarvalRuntimeShared
        ${NARVAL_AST_LINK_GROUP})
endif()
# Windows: the REPL resolves the nv_* symbols through runtime.dll, and the build script asks
# only for the Narval target — without this dependency the DLL would never be produced there.
if(MSVC AND TARGET NarvalRuntimeDll)
    add_dependencies(Narval NarvalRuntimeDll)
endif()
# The runtime .o files have to be produced before the blob is generated, and building
# just the Narval target would otherwise leave them stale (the embedded copy is what
# the linker uses, so a stale blob silently links an old runtime).
add_dependencies(Narval NarvalRuntimeO NarvalRuntimeNostdO)
# The version is the compiler's own (--version, and the home directory the embedded assets are
# written to): defined on this target alone, so a release that passes a new tag rebuilds
# main.cpp and narval_home.cpp instead of every object in the project.
target_compile_definitions(Narval PRIVATE NARVAL_VERSION="${NARVAL_VERSION}")
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
    # The paths come from NarvalProjectIncludes; only the mode flag is per-target.
    target_compile_definitions(Narval PRIVATE NARVAL_DEV_MODE=$<BOOL:${NARVAL_DEV_MODE}>)
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

# No POSIX o runtime tambem e' uma shared library (o REPL resolve nv_* por ela); no
# Windows so' existe o objeto MinGW embutido no compilador.
if(NOT MSVC)
    install(TARGETS NarvalRuntimeShared
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/narval
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}/narval
    )
endif()

install(FILES ${LIB_DIR}/runtime.o
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/narval
    OPTIONAL
)
