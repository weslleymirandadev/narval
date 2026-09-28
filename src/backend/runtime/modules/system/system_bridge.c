// system_bridge.c — o módulo `system`: ambiente e diretório do usuário.
//
// Sem handle e sem estado: o que o processo sabe do ambiente, mais nada. É pequeno de
// propósito — é o mínimo que os caminhos (~/.hosts69 do bait) precisam, e serve de segundo
// módulo no padrão do RUNTIME_MODULES_DESIGN.md (o crypto é o primeiro e tem a mecânica
// completa: .def, declarações geradas, wrappers por macro). C puro: nada de C++ aqui.
#include <stdio.h>
#include <stdlib.h>

#include "backend/runtime/net_bridge.h"          // NvObject/box helpers vêm de nv_runtime.h
#include "backend/runtime/modules/system_decls.h"

const char* nv_system_env(const char* name) {
    const char* v = getenv(name);
    return v ? v : "";                            // ausente é "" e o chamador decide o default
}

const char* nv_system_home(void) {
#ifdef _WIN32
    const char* h = getenv("USERPROFILE");
#else
    const char* h = getenv("HOME");
#endif
    return h ? h : "";
}

// A forma do wrapper é o par (retorno, params) das linhas em system.def: string entra como
// str e sai boxed como str — o mesmo par usado na ponte do crypto.
static const char* arg_str(NvObject* o) {
    return (o && o->ob_type == NVStr_Type) ? ((NVStr*)o)->value : "";
}

static NvObject* box_str(const char* text) {
    Value out = {NULL};
    create_str(&out, text ? text : "");
    return out.obj;
}

NvObject* nv_system_env_builtin(NvObject* arg) {
    return box_str(nv_system_env(arg_str(arg)));
}

NvObject* nv_system_home_builtin(void) {
    return box_str(nv_system_home());
}

// Os inteiros precisam do seu par de conversão (cada ponte tem os seus, estáticos).
static long long arg_int(NvObject* o, long long fallback) {
    if (!o) return fallback;
    if (o->ob_type == NVInt_Type) return (long long)((NVInt*)o)->value;
    return fallback;
}

static NvObject* box_int(long long v) {
    Value out = {NULL};
    create_int(&out, (int64_t)v);
    return out.obj;
}

/* --- argumentos do programa: system.argc() / system.argv(i) ---
 * O binário roda como processo próprio, então os argumentos dele SÃO os do programa (o
 * `narval` passa o que vem depois do arquivo .nv). No POSIX a fonte é /proc/self/cmdline;
 * no MinGW, __argc/__argv. O argv[0] (o caminho do binário) não conta: argv(0) é o primeiro
 * argumento que o usuário escreveu. */
#ifdef _WIN32
extern int __argc;
extern char** __argv;

int nv_system_argc(void) { return __argc > 0 ? __argc - 1 : 0; }

const char* nv_system_argv(int i) {
    if (i < 0 || i + 1 >= __argc) return "";
    return __argv[i + 1] ? __argv[i + 1] : "";
}
#else
static char g_cmdline[8192];
static char* g_argv[64];
static int g_argc;
static int g_args_lidos;

static void nv_system_args_init(void) {
    if (g_args_lidos) return;
    g_args_lidos = 1;
    FILE* f = fopen("/proc/self/cmdline", "rb");
    if (!f) return;
    size_t n = fread(g_cmdline, 1, sizeof(g_cmdline) - 1, f);
    fclose(f);
    g_cmdline[n] = '\0';
    for (size_t i = 0; i < n && g_argc < (int)(sizeof(g_argv) / sizeof(g_argv[0]));) {
        g_argv[g_argc++] = g_cmdline + i;
        while (i < n && g_cmdline[i]) i++;
        i++;                                    /* pula o NUL terminador */
    }
}

int nv_system_argc(void) {
    nv_system_args_init();
    return g_argc > 0 ? g_argc - 1 : 0;         /* argv[0] é o binário */
}

const char* nv_system_argv(int i) {
    nv_system_args_init();
    if (i < 0 || i + 1 >= g_argc) return "";
    return g_argv[i + 1];
}
#endif

NvObject* nv_system_argc_builtin(void) {
    return box_int(nv_system_argc());
}

NvObject* nv_system_argv_builtin(NvObject* i) {
    return box_str(nv_system_argv((int)arg_int(i, -1)));
}
