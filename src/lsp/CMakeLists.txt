option(NARVAL_BUILD_LSP "Build Narval LSP alongside the compiler" ON)
option(NARVAL_BUILD_LSP_ALL_PLATFORMS "Build Linux, Windows and macOS LSP targets when building narval" OFF)
set(NARVAL_LSP_LINUX_TOOLCHAIN "" CACHE FILEPATH "Toolchain file for Linux LSP cross-build")
set(NARVAL_LSP_WINDOWS_TOOLCHAIN "" CACHE FILEPATH "Toolchain file for Windows LSP cross-build")
set(NARVAL_LSP_MACOS_TOOLCHAIN "" CACHE FILEPATH "Toolchain file for macOS LSP cross-build")

set(NARVAL_BUNDLED_NLOHMANN_JSON_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/nlohmann/include")
if(EXISTS "${NARVAL_BUNDLED_NLOHMANN_JSON_INCLUDE_DIR}/nlohmann/json.hpp")
    set(NARVAL_NLOHMANN_JSON_INCLUDE_DIR
        "${NARVAL_BUNDLED_NLOHMANN_JSON_INCLUDE_DIR}"
        CACHE PATH "Directory containing nlohmann/json.hpp" FORCE
    )
else()
    find_path(NARVAL_NLOHMANN_JSON_INCLUDE_DIR
        NAMES nlohmann/json.hpp
        PATHS /usr/local/include /usr/include
    )
    set(NARVAL_NLOHMANN_JSON_INCLUDE_DIR "${NARVAL_NLOHMANN_JSON_INCLUDE_DIR}" CACHE PATH "Directory containing nlohmann/json.hpp")
endif()
if(NOT NARVAL_NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "narval_LSP requires nlohmann/json.hpp. Install nlohmann-json3-dev or set NARVAL_NLOHMANN_JSON_INCLUDE_DIR.")
endif()

if(NOT NARVAL_LSP_WINDOWS_TOOLCHAIN)
    find_program(NARVAL_MINGW_X86_64_CXX x86_64-w64-mingw32-g++)
    find_program(NARVAL_MINGW_X86_64_CC x86_64-w64-mingw32-gcc)
    if(NARVAL_MINGW_X86_64_CXX AND NARVAL_MINGW_X86_64_CC)
        set(NARVAL_LSP_WINDOWS_TOOLCHAIN
            "${PROJECT_SOURCE_DIR}/cmake/toolchains/mingw-w64-x86_64.cmake"
            CACHE FILEPATH "Toolchain file for Windows LSP cross-build" FORCE
        )
        message(STATUS "Narval LSP: found MinGW, enabling Windows LSP target via ${NARVAL_LSP_WINDOWS_TOOLCHAIN}")
    endif()
endif()

set(NARVAL_LSP_CORE_SOURCES
    analyzer.cpp
    diagnostics.cpp
    json_utils.cpp
    path_utils.cpp
    semantic_tokens.cpp
    server.cpp
    transport.cpp
)

add_library(narval_lsp_core STATIC ${NARVAL_LSP_CORE_SOURCES})
target_include_directories(narval_lsp_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${NARVAL_NLOHMANN_JSON_INCLUDE_DIR}
)
target_link_libraries(narval_lsp_core PUBLIC narval_project_includes lexer parser checker)

function(narval_add_lsp_target target_name)
    add_executable(${target_name}
        ${CMAKE_CURRENT_SOURCE_DIR}/narval_LSP.cpp
        ${PROJECT_SOURCE_DIR}/src/frontend/module_manager_codegen_stubs.cpp
    )
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        OUTPUT_NAME narval-lsp
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        target_link_options(${target_name} PRIVATE -W -fPIE)
    endif()
    target_link_libraries(${target_name} PRIVATE narval_lsp_core)
endfunction()

if(NARVAL_BUILD_LSP)
    narval_add_lsp_target(narval_LSP)
endif()

function(narval_add_lsp_platform_target platform_name toolchain_file)
    set(platform_build_dir "${CMAKE_BINARY_DIR}/lsp-${platform_name}")

    if(toolchain_file)
        add_custom_target(narval_LSP_${platform_name}
            COMMAND ${CMAKE_COMMAND}
                -S ${PROJECT_SOURCE_DIR}
                -B ${platform_build_dir}
                -DNARVAL_BUILD_LSP=ON
                -DNARVAL_BUILD_LSP_ALL_PLATFORMS=OFF
                -DNARVAL_NLOHMANN_JSON_INCLUDE_DIR=${NARVAL_NLOHMANN_JSON_INCLUDE_DIR}
                -DCMAKE_TOOLCHAIN_FILE=${toolchain_file}
            COMMAND ${CMAKE_COMMAND}
                --build ${platform_build_dir}
                --target narval_LSP
            COMMENT "Building Narval LSP for ${platform_name}"
            VERBATIM
        )
    elseif(platform_name STREQUAL NARVAL_HOST_PLATFORM AND TARGET narval_LSP)
        add_custom_target(narval_LSP_${platform_name}
            DEPENDS narval_LSP
            COMMENT "Narval LSP for ${platform_name} is the native narval_LSP target"
        )
    else()
        string(TOUPPER "${platform_name}" platform_name_upper)
        add_custom_target(narval_LSP_${platform_name}
            COMMAND ${CMAKE_COMMAND} -E echo
                "Skipping narval_LSP_${platform_name}: set NARVAL_LSP_${platform_name_upper}_TOOLCHAIN to cross-build this platform."
            VERBATIM
        )
    endif()
endfunction()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" CMAKE_SYSTEM_NAME_LOWER)
set(NARVAL_HOST_PLATFORM "${CMAKE_SYSTEM_NAME_LOWER}")
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(NARVAL_HOST_PLATFORM "macos")
elseif(WIN32)
    set(NARVAL_HOST_PLATFORM "windows")
endif()

narval_add_lsp_platform_target(linux "${NARVAL_LSP_LINUX_TOOLCHAIN}")
narval_add_lsp_platform_target(windows "${NARVAL_LSP_WINDOWS_TOOLCHAIN}")
narval_add_lsp_platform_target(macos "${NARVAL_LSP_MACOS_TOOLCHAIN}")

add_custom_target(narval_LSP_all_platforms
    DEPENDS narval_LSP_linux narval_LSP_windows narval_LSP_macos
)
