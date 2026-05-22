narval_collect_sources(RUNTIME_SOURCES
    PATTERNS "*.c"
    EXCLUDE_REGEXES ".*/nostd/.*"
)

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
