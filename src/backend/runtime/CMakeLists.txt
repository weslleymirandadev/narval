# src/backend/runtime/CMakeLists.txt

file(GLOB RUNTIME_SOURCES "*.c")

add_library(runtime_objs OBJECT ${RUNTIME_SOURCES})
target_include_directories(runtime_objs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Criar biblioteca compartilhada para uso no JIT
add_library(runtime_shared SHARED ${RUNTIME_SOURCES})
target_include_directories(runtime_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(runtime_shared PROPERTIES OUTPUT_NAME "runtime")

# Copiar runtime.so para LIB_DIR também
add_custom_command(
    TARGET runtime_shared POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:runtime_shared> ${LIB_DIR}/runtime.so
    COMMENT "Copying runtime.so to lib directory"
)

# FORÇA A GERAÇÃO DO runtime.o SEMPRE que alguém usar esta biblioteca
add_custom_command(
    OUTPUT ${LIB_DIR}/runtime.o
    COMMAND_EXPAND_LISTS
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_DIR}
    COMMAND ${CMAKE_LINKER} -r -o ${LIB_DIR}/runtime.o $<TARGET_OBJECTS:runtime_objs>
    DEPENDS runtime_objs
    COMMENT "Juntando runtime em um único runtime.o"
    VERBATIM
)

# Cria um alvo que realmente dispara o comando acima e o inclui no build padrão
add_custom_target(runtime_o ALL
    DEPENDS ${LIB_DIR}/runtime.o
)
