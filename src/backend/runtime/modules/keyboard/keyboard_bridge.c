// keyboard_bridge.c — o teclado sem bloquear: poll(2) no stdin e a linha quando ela existe.
//
// O primitivo `read()` da linguagem e getchar() ate o \n (bloqueante); para um chat e preciso
// perguntar ANTES. E a mesma ideia do poll dos sockets, aplicada ao descritor 0.
#include <stdio.h>
#include <string.h>

#include "backend/runtime/net_bridge.h"
#include "backend/runtime/modules/keyboard_decls.h"

#ifndef _WIN32
#include <poll.h>

int nv_keyboard_ready(int timeout_ms)
{
    struct pollfd p;
    p.fd = 0;                       /* stdin */
    p.events = POLLIN;
    p.revents = 0;
    int r = poll(&p, 1, timeout_ms < 0 ? -1 : timeout_ms);
    return (r > 0 && (p.revents & POLLIN)) ? 1 : 0;
}
#else
#include <conio.h>
/* MinGW nao tem poll de verdade no descritor 0: o caminho do console e kbhit(). */
int nv_keyboard_ready(int timeout_ms)
{
    (void)timeout_ms;
    return _kbhit() ? 1 : 0;
}
#endif

const char* nv_keyboard_line(void)
{
    static char buf[4096];
    if (!fgets(buf, sizeof buf, stdin)) return "";
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    return buf;
}

static long long arg_int(NvObject* o, long long fallback)
{
    if (!o) return fallback;
    if (o->ob_type == NVInt_Type) return (long long)((NVInt*)o)->value;
    return fallback;
}

static NvObject* box_str(const char* s) { Value out = {NULL}; create_str(&out, s ? s : ""); return out.obj; }
static NvObject* box_int(long long v) { Value out = {NULL}; create_int(&out, (int64_t)v); return out.obj; }

NvObject* nv_keyboard_ready_builtin(NvObject* ms) { return box_int(nv_keyboard_ready((int)arg_int(ms, 0))); }
NvObject* nv_keyboard_line_builtin(void) { return box_str(nv_keyboard_line()); }
