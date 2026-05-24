option(BUILD_REPL_MODE "Build REPL (Read-Eval-Print Loop) support" ON)
option(BUILD_NOTEBOOK_MODE "Build Jupyter-like notebook support" ON)
option(ENABLE_INTERACTIVE_DEBUG "Enable debug features for interactive mode" OFF)

narval_collect_sources(INTERACTIVE_SOURCES)
list(APPEND INTERACTIVE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/compilation_engine_nir_stub.cpp
)

add_library(NarvalInteractive ${INTERACTIVE_SOURCES})
target_include_directories(NarvalInteractive PRIVATE ${PROJECT_SOURCE_DIR}/include)

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

find_package(PkgConfig QUIET)

if(BUILD_REPL_MODE)
    if(PkgConfig_FOUND)
        pkg_check_modules(READLINE QUIET readline)
    endif()

    if(NOT READLINE_FOUND)
        find_package(ReadLine QUIET)
        if(ReadLine_FOUND)
            set(READLINE_LIBRARIES ${READLINE_LIBRARY})
            set(READLINE_INCLUDE_DIRS ${READLINE_INCLUDE_DIR})
            set(READLINE_FOUND TRUE)
        endif()
    endif()

    if(NOT READLINE_FOUND)
        find_library(READLINE_LIBRARIES NAMES readline PATHS /usr/lib /usr/local/lib)
        find_path(READLINE_INCLUDE_DIRS readline/readline.h PATHS /usr/include /usr/local/include)
        if(READLINE_LIBRARIES AND READLINE_INCLUDE_DIRS)
            set(READLINE_FOUND TRUE)
            message(STATUS "Found readline: ${READLINE_LIBRARIES}")
        else()
            message(WARNING "readline not found - REPL will have limited line editing")
            set(READLINE_FOUND FALSE)
        endif()
    endif()
endif()

if(BUILD_NOTEBOOK_MODE)
    find_package(nlohmann_json QUIET)
    if(NOT nlohmann_json_FOUND)
        if(PkgConfig_FOUND)
            pkg_check_modules(NLOHMANN_JSON QUIET nlohmann_json)
        endif()

        if(NOT NLOHMANN_JSON_FOUND)
            find_path(NLOHMANN_JSON_INCLUDE_DIR
                NAMES nlohmann/json.hpp
                PATHS /usr/include /usr/local/include /opt/homebrew/include
                PATH_SUFFIXES nlohmann
            )
            if(NLOHMANN_JSON_INCLUDE_DIR)
                set(NLOHMANN_JSON_FOUND TRUE)
                set(NLOHMANN_JSON_INCLUDE_DIRS ${NLOHMANN_JSON_INCLUDE_DIR})
            endif()
        endif()

        if(NOT NLOHMANN_JSON_FOUND)
            message(WARNING "nlohmann/json not found - notebook save/load disabled")
            set(NLOHMANN_JSON_FOUND FALSE)
        endif()
    endif()
endif()

target_link_libraries(NarvalInteractive PRIVATE
    NarvalAllCommon
    NarvalChecker
    NarvalNirDialect
    ${LLVM_LIBS}
    ${LLVM_SYS_LIBS}
)
target_sources(NarvalInteractive PRIVATE
    ${PROJECT_SOURCE_DIR}/src/frontend/module_manager_codegen_stubs.cpp
)

if(BUILD_REPL_MODE)
    target_compile_definitions(NarvalInteractive PRIVATE ENABLE_REPL_MODE)
    if(READLINE_FOUND)
        target_link_libraries(NarvalInteractive PRIVATE ${READLINE_LIBRARIES})
        target_include_directories(NarvalInteractive PRIVATE ${READLINE_INCLUDE_DIRS})
        if(READLINE_CFLAGS_OTHER)
            target_compile_options(NarvalInteractive PRIVATE ${READLINE_CFLAGS_OTHER})
        endif()
        target_compile_definitions(NarvalInteractive PRIVATE HAVE_READLINE)
        message(STATUS "Interactive: REPL mode enabled with readline support")
    else()
        message(STATUS "Interactive: REPL mode enabled (basic line editing)")
    endif()
endif()

if(BUILD_NOTEBOOK_MODE)
    target_compile_definitions(NarvalInteractive PRIVATE ENABLE_NOTEBOOK_MODE)
    if(NLOHMANN_JSON_FOUND)
        target_include_directories(NarvalInteractive PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
        target_compile_definitions(NarvalInteractive PRIVATE HAVE_NLOHMANN_JSON)
        message(STATUS "Interactive: Notebook mode enabled with JSON support")
    else()
        message(STATUS "Interactive: Notebook mode enabled (JSON serialization disabled)")
    endif()
endif()

if(ENABLE_INTERACTIVE_DEBUG)
    target_compile_definitions(NarvalInteractive PRIVATE INTERACTIVE_DEBUG=1)
    message(STATUS "Interactive: Debug mode enabled")
endif()

target_compile_definitions(NarvalInteractive PRIVATE
    INTERACTIVE_MODE=1
    JIT_OPTIMIZATION_LEVEL=1
    FAST_EXPRESSION_EVAL=1
    NARVAL_USE_NIR=1
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(NarvalInteractive PRIVATE
        -O2
        -fno-omit-frame-pointer
        -fno-strict-aliasing
        -funroll-loops
    )
endif()

if(WIN32)
    target_compile_definitions(NarvalInteractive PRIVATE
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
endif()

message(STATUS "Interactive module configuration:")
message(STATUS "  REPL Mode: ${BUILD_REPL_MODE}")
message(STATUS "  Notebook Mode: ${BUILD_NOTEBOOK_MODE}")
message(STATUS "  Debug Mode: ${ENABLE_INTERACTIVE_DEBUG}")
