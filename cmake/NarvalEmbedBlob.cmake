# NarvalEmbedBlob.cmake — gera o .cpp que carrega os objetos do runtime dentro do compilador.
#
# Mesmo formato do `xxd -i` que isso substitui, mas sem depender de `sh` nem de `xxd` (que o
# build do Windows nao tem). O compilador escreve o blob num arquivo temporario e linka o
# programa contra ele, entao o conteudo tem que ser o objeto byte a byte.
#
# Uso:
#   cmake -DOUT=<saida.cpp> -DSTD_OBJ=<runtime.o> -DNOSTD_OBJ=<runtime_nostd.o> \
#         -P cmake/NarvalEmbedBlob.cmake
#
# Um objeto ausente vira um array de tamanho zero: e' o caso do runtime no_std no Windows
# (syscalls Linux), onde o compilador recusa @[no_std] antes de tentar linkar.

if(NOT OUT)
    message(FATAL_ERROR "NarvalEmbedBlob: OUT nao definido")
endif()

set(blob "#include <cstddef>\n")

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

file(WRITE "${OUT}" "${blob}")
