# src/backend/runtime/CMakeLists.txt

narval_collect_sources(RUNTIME_SOURCES
    PATTERNS "*.c"
)

add_library(runtime_objs OBJECT ${RUNTIME_SOURCES})
target_include_directories(runtime_objs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include)
# Cada função em sua própria section: permite --gc-sections eliminar símbolos não usados
target_compile_options(runtime_objs PRIVATE -ffunction-sections -fdata-sections)

# Criar biblioteca compartilhada para uso no JIT
add_library(runtime_shared SHARED ${RUNTIME_SOURCES})
target_include_directories(runtime_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include)
target_compile_options(runtime_shared PRIVATE -ffunction-sections -fdata-sections)
set_target_properties(runtime_shared PROPERTIES OUTPUT_NAME "runtime" PREFIX "")

# Copiar runtime.so para LIB_DIR e regenerar runtime.o a cada build
add_custom_command(
    TARGET runtime_shared POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:runtime_shared> ${LIB_DIR}/runtime.so
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_DIR}
    COMMAND ${CMAKE_LINKER} -r -o ${LIB_DIR}/runtime.o $<TARGET_OBJECTS:runtime_objs>
    COMMENT "Atualizando runtime.so e runtime.o em lib/"
    COMMAND_EXPAND_LISTS
)

# Alvo legado para compatibilidade com dependências externas.
# Deve rodar sempre: o compilador procura build/lib/runtime.o em tempo de
# execução, e apagar esse arquivo não força o POST_BUILD de runtime_shared.
add_custom_target(runtime_o ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_DIR}
    COMMAND ${CMAKE_LINKER} -r -o ${LIB_DIR}/runtime.o $<TARGET_OBJECTS:runtime_objs>
    DEPENDS runtime_objs runtime_shared
    COMMENT "Regenerando runtime.o em lib/"
    COMMAND_EXPAND_LISTS
)
