# =============================================================================
# Narval Interactive Module - REPL & Notebook Support
# =============================================================================

# Configuration options
option(BUILD_REPL_MODE "Build REPL (Read-Eval-Print Loop) support" ON)
option(BUILD_NOTEBOOK_MODE "Build Jupyter-like notebook support" ON)
option(ENABLE_INTERACTIVE_DEBUG "Enable debug features for interactive mode" OFF)

# Source collection (explicitly search this directory and subdirectories)
file(GLOB_RECURSE INTERACTIVE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Core interactive library
# Core interactive library
add_library(interactive ${INTERACTIVE_SOURCES})

# Ensure project headers are available to the interactive target
target_include_directories(interactive PRIVATE ${PROJECT_SOURCE_DIR}/include)

# Basic includes
include_directories(${PROJECT_SOURCE_DIR}/include)

# LLVM configuration from root CMakeLists.txt
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# =============================================================================
# Optional Dependencies Detection
# =============================================================================

find_package(PkgConfig QUIET)

# Readline/Line editing support (for REPL)
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

# JSON support (for Notebook serialization)
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

# =============================================================================
# Core Library Linking
# =============================================================================

target_link_libraries(interactive PRIVATE 
    narval_all_common 
    checker 
    generator
    ${LLVM_LIBS}
    ${LLVM_SYS_LIBS}
)

# =============================================================================
# Feature-specific Linking and Definitions
# =============================================================================

# REPL Mode Support
if(BUILD_REPL_MODE)
    target_compile_definitions(interactive PRIVATE ENABLE_REPL_MODE)
    if(READLINE_FOUND)
        target_link_libraries(interactive PRIVATE ${READLINE_LIBRARIES})
        target_include_directories(interactive PRIVATE ${READLINE_INCLUDE_DIRS})
        if(READLINE_CFLAGS_OTHER)
            target_compile_options(interactive PRIVATE ${READLINE_CFLAGS_OTHER})
        endif()
        target_compile_definitions(interactive PRIVATE HAVE_READLINE)
        message(STATUS "Interactive: REPL mode enabled with readline support")
    else()
        message(STATUS "Interactive: REPL mode enabled (basic line editing)")
    endif()
endif()

# Notebook Mode Support
if(BUILD_NOTEBOOK_MODE)
    target_compile_definitions(interactive PRIVATE ENABLE_NOTEBOOK_MODE)
    if(NLOHMANN_JSON_FOUND)
        target_include_directories(interactive PRIVATE ${NLOHMANN_JSON_INCLUDE_DIRS})
        target_compile_definitions(interactive PRIVATE HAVE_NLOHMANN_JSON)
        message(STATUS "Interactive: Notebook mode enabled with JSON support")
    else()
        message(STATUS "Interactive: Notebook mode enabled (JSON serialization disabled)")
    endif()
endif()

# Debug Support
if(ENABLE_INTERACTIVE_DEBUG)
    target_compile_definitions(interactive PRIVATE INTERACTIVE_DEBUG=1)
    message(STATUS "Interactive: Debug mode enabled")
endif()

# =============================================================================
# Performance Optimizations for Interactive Use
# =============================================================================

# Base interactive mode definitions
target_compile_definitions(interactive PRIVATE 
    INTERACTIVE_MODE=1
    JIT_OPTIMIZATION_LEVEL=1
    FAST_EXPRESSION_EVAL=1
)

# Compiler-specific optimizations for low latency
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(interactive PRIVATE 
        -O2  # Balanced optimization for interactive use
        -fno-omit-frame-pointer  # Better debugging/profiling
        -fno-strict-aliasing   # Safer optimizations for dynamic code
        -funroll-loops         # Small loop unrolling for hot paths
    )
endif()

# =============================================================================
# Platform-specific Settings
# =============================================================================

if(WIN32)
    target_compile_definitions(interactive PRIVATE 
        _WIN32_WINNT=0x0601  # Windows 7+
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
endif()

# =============================================================================
# Summary
# =============================================================================

message(STATUS "Interactive module configuration:")
message(STATUS "  REPL Mode: ${BUILD_REPL_MODE}")
message(STATUS "  Notebook Mode: ${BUILD_NOTEBOOK_MODE}")
message(STATUS "  Debug Mode: ${ENABLE_INTERACTIVE_DEBUG}")
