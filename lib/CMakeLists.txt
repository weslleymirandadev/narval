# Compila arquivos .ll em objetos e agrega em uma lib estática

# Ferramentas necessárias: llvm-link (para unir IR) e llc (para gerar objeto)
find_program(LLVM_LINK_EXECUTABLE NAMES llvm-link)
if(NOT LLVM_LINK_EXECUTABLE)
  message(FATAL_ERROR "llvm-link not found. Please install LLVM tools (llvm).")
endif()
find_program(LLC_EXECUTABLE NAMES llc)
if(NOT LLC_EXECUTABLE)
  message(FATAL_ERROR "llc not found. Please install LLVM tools (llvm).")
endif()

# Coleta todos os .ll neste diretório
file(GLOB_RECURSE LL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.ll")
file(GLOB_RECURSE C_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

find_program(CLANG_EXECUTABLE NAMES clang)
if(NOT CLANG_EXECUTABLE)
  message(FATAL_ERROR "clang not found. Please install clang.")
endif()

set(PROJECT_ROOT_INCLUDE "${CMAKE_SOURCE_DIR}/include")

set(C_BCS)
foreach(src ${C_SOURCES})
  get_filename_component(name "${src}" NAME_WE)
  set(out_bc "${LIB_DIR}/${name}.bc")
  add_custom_command(
    OUTPUT "${out_bc}"
    COMMAND ${CLANG_EXECUTABLE} -emit-llvm -c -O2 -I "${PROJECT_ROOT_INCLUDE}" -o "${out_bc}" "${src}"
    DEPENDS "${src}"
    VERBATIM
  )
  list(APPEND C_BCS "${out_bc}")
endforeach()

# Caminhos de saída
set(STD_BC "${LIB_DIR}/std.bc")
set(STD_OBJ "${LIB_DIR}/std.o")

# Une todos os .ll em um único bitcode
add_custom_command(
  OUTPUT "${STD_BC}"
  COMMAND ${LLVM_LINK_EXECUTABLE} -o "${STD_BC}" ${LL_SOURCES} ${C_BCS}
  DEPENDS ${LL_SOURCES} ${C_BCS}
  COMMENT "Linking LLVM IR files -> ${STD_BC}"
  VERBATIM
)

# Compila o bitcode único para objeto std.o
add_custom_command(
  OUTPUT "${STD_OBJ}"
  COMMAND ${LLC_EXECUTABLE} -filetype=obj -o "${STD_OBJ}" "${STD_BC}"
  DEPENDS "${STD_BC}"
  COMMENT "Compiling ${STD_BC} -> ${STD_OBJ}"
  VERBATIM
)

# Alvo para construir o objeto
add_custom_target(std_o ALL DEPENDS "${STD_OBJ}")

# Exporta o caminho do objeto para o CMake raiz
set(STD_LL_OBJECT "${STD_OBJ}" CACHE INTERNAL "Single object built from lib/*.ll")
