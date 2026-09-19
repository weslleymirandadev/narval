// system_bridge.c — o módulo `system`: ambiente e diretório do usuário.
//
// Sem handle e sem estado: o que o processo sabe do ambiente, mais nada. É pequeno de
// propósito — é o mínimo que os caminhos (~/.hosts69 do bait) precisam, e serve de segundo
// módulo no padrão do RUNTIME_MODULES_DESIGN.md (o crypto é o primeiro e tem a mecânica
// completa: .def, declarações geradas, wrappers por macro). C puro: nada de C++ aqui.
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
