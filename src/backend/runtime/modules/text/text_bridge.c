// text_bridge.c — o módulo `text`: byte <-> código.
//
// Sem estado e sem handle: duas funções de uma linha cada. C puro, como as outras pontes.
// Existe porque a linguagem trata string como byte e não tinha como dizer "o byte 0x61 é
// 'a'" — o que travava a migração de hex_to_str/str_to_hex para Narval.
#include <string.h>

#include "backend/runtime/net_bridge.h"
#include "backend/runtime/modules/text_decls.h"


// A forma dos wrappers é o par (retorno, params) das linhas do text.def — e os helpers de
// conversão são ESTÁTICOS por ponte, como nas do system/crypto/net (cada .c tem os seus).
static const char* arg_str(NvObject* o) {
    return (o && o->ob_type == NVStr_Type) ? ((NVStr*)o)->value : "";
}

static long long arg_int(NvObject* o, long long fallback) {
    if (!o) return fallback;
    if (o->ob_type == NVInt_Type) return (long long)((NVInt*)o)->value;
    return fallback;
}

static NvObject* box_str(const char* s) {
    Value out = {NULL};
    create_str(&out, s ? s : "");
    return out.obj;
}

static NvObject* box_int(long long v) {
    Value out = {NULL};
    create_int(&out, (int64_t)v);
    return out.obj;
}

// Código (0..255) -> string de um byte. Fora da faixa devolve "" (o chamador decide).
const char* nv_text_chr(int code) {
    static char b[2];
    if (code < 0 || code > 255) return "";
    b[0] = (char)code;
    b[1] = 0;
    return b;
}

// Valor do PRIMEIRO byte; string vazia devolve -1.
int nv_text_ord(const char* s) {
    if (!s || !s[0]) return -1;
    return (int)(unsigned char)s[0];
}

NvObject* nv_text_chr_builtin(NvObject* code) { return box_str(nv_text_chr((int)arg_int(code, -1))); }
NvObject* nv_text_ord_builtin(NvObject* s) { return box_int(nv_text_ord(arg_str(s))); }
