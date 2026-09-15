narval_collect_sources(RUNTIME_SOURCES
    PATTERNS "*.c"
    EXCLUDE_REGEXES ".*/nostd/.*"
)

# ── Windows: o runtime embutido no compilador vem do gcc do MinGW ────────────
# O narval.exe linka os programas com o gcc do MinGW (src/main.cpp), entao o runtime que
# ele carrega EMBUTIDO precisa ser objeto MinGW. cl.exe nao serve para isso: nao existe
# equivalente ao `ld -r` e o runtime e' C POSIX (unistd/dlfcn). Aqui o runtime e' compilado
# pelo gcc do MinGW e unido com `gcc -r`.
#
# Nada do runtime e' linkado dentro do narval.exe daqui: os dois alvos MSVC abaixo existem
# so' no POSIX (o REPL do Linux resolve os simbolos nv_* pelo processo). Consequencia
# assumida: no Windows o REPL/JIT nao resolve esses simbolos.
if(MSVC)
    find_program(NARVAL_MINGW_GCC NAMES gcc gcc.exe)
    if(NOT NARVAL_MINGW_GCC)
        message(FATAL_ERROR
            "gcc (MinGW) nao encontrado no PATH. E' ele que compila o runtime embutido no "
            "narval.exe; no Windows o compilador tambem chama esse gcc para linkar os "
            "programas gerados.")
    endif()

    set(WIN_RT_OBJ_DIR ${CMAKE_CURRENT_BINARY_DIR}/mingw-obj)
    file(MAKE_DIRECTORY ${WIN_RT_OBJ_DIR})
    set(WIN_RT_OBJS)
    foreach(rt_src ${RUNTIME_SOURCES})
        get_filename_component(rt_name ${rt_src} NAME)
        set(rt_obj ${WIN_RT_OBJ_DIR}/${rt_name}.o)
        # -std=gnu17: o mesmo padrao que o gcc do Linux usa por padrao. O gcc 15 do MinGW
        # aceita C23 e transforma em erro o que ali e' so' aviso (implicit declarations).
        add_custom_command(
            OUTPUT ${rt_obj}
            COMMAND ${NARVAL_MINGW_GCC} -c -std=gnu17 -O1
                    -ffunction-sections -fdata-sections
                    -I ${PROJECT_SOURCE_DIR}/include -I ${CMAKE_CURRENT_SOURCE_DIR}
                    -o ${rt_obj} ${rt_src}
            DEPENDS ${rt_src}
            COMMENT "MinGW: ${rt_name} (runtime embutido)"
            VERBATIM
        )
        list(APPEND WIN_RT_OBJS ${rt_obj})
    endforeach()

    add_custom_target(NarvalRuntimeO ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_DIR}
        COMMAND ${NARVAL_MINGW_GCC} -r -o ${LIB_DIR}/runtime.o ${WIN_RT_OBJS}
        DEPENDS ${WIN_RT_OBJS}
        COMMENT "Unindo runtime.o (MinGW) para o compilador do Windows"
        COMMAND_EXPAND_LISTS
    )

    # O runtime no_std e' syscall POSIX (mmap/read/write/exit direto): no Windows nao ha'
    # objeto e o compilador recusa @[no_std] antes de linkar (src/main.cpp).
    add_custom_target(NarvalRuntimeNostdO ALL
        COMMENT "no_std: POSIX only, sem objeto no Windows"
    )

    # runtime.dll: no Windows nada do runtime esta' dentro do narval.exe, entao e' por ela
    # que o REPL resolve os simbolos nv_* (ver repl_state.cpp, que procura runtime.dll em
    # build-win/lib). -static evita depender de libwinpthread-1.dll em tempo de execucao.
    add_custom_target(NarvalRuntimeDll ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_DIR}
        COMMAND ${NARVAL_MINGW_GCC} -shared -static -Wl,--export-all-symbols
                -o ${LIB_DIR}/runtime.dll ${WIN_RT_OBJS}
        DEPENDS ${WIN_RT_OBJS}
        COMMENT "MinGW: runtime.dll (REPL)"
        COMMAND_EXPAND_LISTS
    )

    # Os testes linkam estes dois alvos (nunca construidos no Windows): INTERFACE mantem as
    # linhas de link validas sem compilar nada com o cl.exe.
    add_library(NarvalRuntimeObjs INTERFACE)
    add_library(NarvalRuntimeShared INTERFACE)
    return()
endif()

add_library(NarvalRuntimeObjs OBJECT ${RUNTIME_SOURCES})
target_include_directories(NarvalRuntimeObjs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include)
target_compile_options(NarvalRuntimeObjs PRIVATE -ffunction-sections -fdata-sections)

add_library(NarvalRuntimeShared SHARED ${RUNTIME_SOURCES})
target_include_directories(NarvalRuntimeShared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include)
target_compile_options(NarvalRuntimeShared PRIVATE -ffunction-sections -fdata-sections)
set_target_properties(NarvalRuntimeShared PROPERTIES OUTPUT_NAME "runtime" PREFIX "")

add_custom_command(
    TARGET NarvalRuntimeShared POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:NarvalRuntimeShared> ${LIB_DIR}/runtime.so
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_DIR}
    COMMAND ${CMAKE_LINKER} -r -o ${LIB_DIR}/runtime.o $<TARGET_OBJECTS:NarvalRuntimeObjs>
    COMMENT "Updating runtime.so and runtime.o in lib/"
    COMMAND_EXPAND_LISTS
)

add_library(NarvalRuntimeNostdObjs OBJECT nostd/runtime_nostd.c)
target_include_directories(NarvalRuntimeNostdObjs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include)
target_compile_options(NarvalRuntimeNostdObjs PRIVATE -ffunction-sections -fdata-sections -ffreestanding)

add_custom_target(NarvalRuntimeNostdO ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_DIR}
    COMMAND ${CMAKE_LINKER} -r -o ${LIB_DIR}/runtime_nostd.o $<TARGET_OBJECTS:NarvalRuntimeNostdObjs>
    DEPENDS NarvalRuntimeNostdObjs
    COMMENT "Generating runtime_nostd.o in lib/"
    COMMAND_EXPAND_LISTS
)

add_custom_target(NarvalRuntimeO ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIB_DIR}
    COMMAND ${CMAKE_LINKER} -r -o ${LIB_DIR}/runtime.o $<TARGET_OBJECTS:NarvalRuntimeObjs>
    DEPENDS NarvalRuntimeObjs NarvalRuntimeShared
    COMMENT "Regenerating runtime.o in lib/"
    COMMAND_EXPAND_LISTS
)
