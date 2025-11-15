cmake_minimum_required(VERSION 3.10)
project(Ruphi)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# === DEFINIR LIB_DIR NA RAIZ ===
set(LIB_DIR ${CMAKE_CURRENT_BINARY_DIR}/lib)
file(MAKE_DIRECTORY ${LIB_DIR})

# === Passa para subdiretórios ===
set(LIB_DIR ${LIB_DIR} CACHE INTERNAL "Pasta de arquivos gerados")

include_directories(${PROJECT_SOURCE_DIR}/include)

add_library(ruphi_project_includes INTERFACE)
target_include_directories(ruphi_project_includes INTERFACE ${PROJECT_SOURCE_DIR}/include)

find_package(LLVM REQUIRED CONFIG)
execute_process(COMMAND llvm-config --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS)
execute_process(COMMAND llvm-config --libs OUTPUT_VARIABLE LLVM_LIBS)
execute_process(COMMAND llvm-config --system-libs OUTPUT_VARIABLE LLVM_SYS_LIBS)
execute_process(COMMAND llvm-config --ldflags OUTPUT_VARIABLE LLVM_LDFLAGS)

string(STRIP "${LLVM_CXX_FLAGS}" LLVM_CXX_FLAGS)
string(STRIP "${LLVM_LIBS}" LLVM_LIBS)
string(STRIP "${LLVM_SYS_LIBS}" LLVM_SYS_LIBS)
string(STRIP "${LLVM_LDFLAGS}" LLVM_LDFLAGS)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${LLVM_CXX_FLAGS} -fexceptions")

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_library(ruphi_common INTERFACE)
target_compile_options(ruphi_common INTERFACE -fexceptions)
target_include_directories(ruphi_common INTERFACE ${LLVM_INCLUDE_DIRS})
target_link_directories(ruphi_common INTERFACE ${LLVM_LIBRARY_DIRS})
target_compile_definitions(ruphi_common INTERFACE ${LLVM_DEFINITIONS})
target_link_libraries(ruphi_common INTERFACE ${LLVM_LIBS} ${LLVM_SYS_LIBS})

add_library(ruphi_all_common INTERFACE)
target_link_libraries(ruphi_all_common INTERFACE ruphi_project_includes ruphi_common m)

function(ruphi_add_test target_name)
    add_executable(${target_name} ${ARGN})
    # target_link_libraries(${target_name} PRIVATE ruphi_all_common lexer parser checker generator)
    target_link_libraries(${target_name} PRIVATE ruphi_all_common lexer parser generator)
    target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/include)
endfunction()

add_subdirectory(src/frontend/lexer)
add_subdirectory(src/frontend/parser)
# add_subdirectory(src/frontend/checker)
add_subdirectory(src/backend/runtime)
add_subdirectory(src/backend/codegen)
add_subdirectory(lib)

add_executable(ruphi src/main.cpp src/frontend/module_manager.cpp)
target_link_options(ruphi PRIVATE -Wl,-no-pie)
target_include_directories(ruphi PRIVATE ${PROJECT_SOURCE_DIR}/include)
# target_link_libraries(ruphi PRIVATE ruphi_all_common lexer parser checker generator runtime_objs ${STD_LL_OBJECT})
target_link_libraries(ruphi PRIVATE ruphi_all_common lexer parser generator runtime_objs ${STD_LL_OBJECT})
add_dependencies(ruphi std_o)

if(TARGET ruphi)
    target_compile_definitions(ruphi PRIVATE
        RUPHI_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
        RUPHI_INCLUDE_DIR="${PROJECT_SOURCE_DIR}/include"
    )
endif()