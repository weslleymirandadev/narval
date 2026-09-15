if(NOT OUT)
    message(FATAL_ERROR "NarvalEmbedBlob: OUT not defined")
endif()

set(blob "#include <cstddef>\n")
string(APPEND blob "#include \"frontend/embedded_assets.hpp\"\n")

foreach(pair IN ITEMS
        "narval_runtime_obj=${STD_OBJ}"
        "narval_runtime_nostd_obj=${NOSTD_OBJ}")
    string(REGEX REPLACE "=.*" ""  name "${pair}")
    string(REGEX REPLACE "^[^=]*=" "" objfile "${pair}")

    if(EXISTS "${objfile}")
        file(READ "${objfile}" hex HEX)
        string(LENGTH "${hex}" hexlen)
        math(EXPR nbytes "${hexlen} / 2")
        string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
        string(APPEND blob "unsigned char ${name}[] = {${bytes}};\n")
        string(APPEND blob "unsigned int ${name}_len = ${nbytes};\n")
    else()
        string(APPEND blob "unsigned char ${name}[] = {0};\n")
        string(APPEND blob "unsigned int ${name}_len = 0;\n")
    endif()
endforeach()

# The stdlib table: one array per file (name of the indexed symbol, to avoid depending on
# valid identifier characters) and the list {name, bytes, size} that the runtime queries.
if(STDLIB_FILES)
    set(index 0)
    set(entries "")
    foreach(filename IN LISTS STDLIB_FILES)
        set(path "${STDLIB_DIR}/${filename}")
        if(NOT EXISTS "${path}")
            message(FATAL_ERROR "NarvalEmbedBlob: stdlib file not found: ${path}")
        endif()
        file(READ "${path}" hex HEX)
        string(LENGTH "${hex}" hexlen)
        math(EXPR nbytes "${hexlen} / 2")
        string(REGEX REPLACE "(..)" "0x\\1," bytes "${hex}")
        string(APPEND blob "static unsigned char narval_stdlib_${index}_data[] = {${bytes}};\n")
        string(APPEND blob
            "static const unsigned int narval_stdlib_${index}_len = ${nbytes};\n")
        string(APPEND blob
            "static const char narval_stdlib_${index}_name[] = \"${filename}\";\n")
        # The entry goes into the table (built at the end, after declarations).
        set(entries "${entries}{narval_stdlib_${index}_name, narval_stdlib_${index}_data, narval_stdlib_${index}_len},\n")
        math(EXPR index "${index} + 1")
    endforeach()

    # Without `static`: the header declares `extern const`, and the previous extern is what
    # provides external linkage to a const object in namespace scope (by default it is internal).
    string(APPEND blob "const narval::EmbeddedFile narval_stdlib_files[] = {\n${entries}};\n")
    string(APPEND blob "const unsigned int narval_stdlib_files_count = ${index};\n")
else()
    string(APPEND blob "const narval::EmbeddedFile narval_stdlib_files[] = {{nullptr, nullptr, 0}};\n")
    string(APPEND blob "const unsigned int narval_stdlib_files_count = 0;\n")
endif()

file(WRITE "${OUT}" "${blob}")
