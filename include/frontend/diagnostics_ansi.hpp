#pragma once

// Cor no diagnóstico SÓ quando o destino é um terminal.
//
// Com cor sempre ligada, a mensagem sai com escapes ANSI mesmo indo para um arquivo: nos bytes
// existe `WARNING` ESC[0m ESC[1m `: `, e qualquer verificação que procure a mensagem contígua
// falha — o terminal renderiza certo, o `grep -F` não. Foi o que fez um caso de teste com o
// stderr VISIVELMENTE correto ser reprovado (ver FOUNDATION_SPEC.md fase 2).
//
// Os arquivos que imprimem diagnóstico definem as suas constantes ANSI_* a partir daqui, então os
// pontos de uso não mudam: `std::cerr << ANSI_BOLD << "..."` continua igual.

#include <cstdio>
#include <unistd.h>

namespace nv::diag {

inline bool color() {
    static const bool on = isatty(fileno(stderr)) != 0;
    return on;
}

} // namespace nv::diag
