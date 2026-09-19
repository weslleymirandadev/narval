// bait_bridge.c — o módulo `bait`: mac1, o filtro de pré-autenticação do stack.
//
// mac1 é WireGuard-style: key = SHA-512("ipv69-mac1" || addr40 BE)[0..31] e a tag é
// Poly1305(key, msg), 16 bytes. Não é autenticação — é filtro de liveness/formato, como diz a
// própria `include/IPv69/mac1.h` do IPv69; quem autentica é a assinatura Ed25519 do INIT.
//
// A cripto mora no crypto vendorizado (`../crypto/ed25519/`), o mesmo que o módulo `crypto`
// usa: aqui só se compõe, como o `src/IPv69/mac1.c` dele compõe. C puro.
#include <stdio.h>
#include <string.h>

#include "backend/runtime/net_bridge.h"                 // NvObject/box helpers
#include "backend/runtime/modules/bait_decls.h"
#include "../crypto/ed25519/ed25519.h"

#define BAIT_MAX_BYTES 4096                             /* ICSP_MAX_PAYLOAD é 1400 */
#define BAIT_FAIL      "!"                              /* nunca um hex válido: dá para detectar */

static int hex_nib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* hex -> bytes; devolve o número de bytes ou (size_t)-1 quando o hex é inválido ou não cabe */
static size_t hex_in(const char* hex, unsigned char* out, size_t cap) {
    size_t n = 0, i = 0;
    if (!hex || (strlen(hex) & 1)) return (size_t)-1;
    while (hex[i]) {
        int hi = hex_nib(hex[i]), lo = hex_nib(hex[i + 1]);
        if (hi < 0 || lo < 0 || n >= cap) return (size_t)-1;
        out[n++] = (unsigned char)((hi << 4) | lo);
        i += 2;
    }
    return n;
}

static const char* hex_out(const unsigned char* b, size_t n) {
    static char buf[BAIT_MAX_BYTES * 2 + 1];
    size_t i;
    if (n > BAIT_MAX_BYTES) return BAIT_FAIL;
    for (i = 0; i < n; i++) sprintf(buf + i * 2, "%02x", b[i]);
    buf[n * 2] = 0;
    return buf;
}

/* key = SHA-512("ipv69-mac1" || addr40 BE)[0..31]; `addr_hex` são os 10 dígitos do endereço */
const char* nv_bait_mac1_key(const char* addr_hex) {
    unsigned char buf[15];
    unsigned char h[64];
    unsigned long long addr = 0;
    int i;
    if (!addr_hex || strlen(addr_hex) != 10) return BAIT_FAIL;
    for (i = 0; i < 10; i++) {
        int v = hex_nib(addr_hex[i]);
        if (v < 0) return BAIT_FAIL;
        addr = (addr << 4) | (unsigned long long)v;
    }
    memcpy(buf, "ipv69-mac1", 10);
    buf[10] = (unsigned char)(addr >> 32);
    buf[11] = (unsigned char)(addr >> 24);
    buf[12] = (unsigned char)(addr >> 16);
    buf[13] = (unsigned char)(addr >> 8);
    buf[14] = (unsigned char)addr;
    ed25519_sha512(h, buf, sizeof buf);
    return hex_out(h, 32);
}

/* a tag: Poly1305(key, msg), 16 bytes */
const char* nv_bait_mac1(const char* msg_hex, const char* key_hex) {
    unsigned char msg[BAIT_MAX_BYTES], key[32], tag[16];
    size_t n = hex_in(msg_hex, msg, sizeof msg);
    if (n == (size_t)-1 || hex_in(key_hex, key, sizeof key) != 32) return BAIT_FAIL;
    ed25519_poly1305(tag, msg, n, key);
    return hex_out(tag, sizeof tag);
}

/* 1 quando a tag confere */
int nv_bait_mac1_verify(const char* tag_hex, const char* msg_hex, const char* key_hex) {
    unsigned char tag[16], msg[BAIT_MAX_BYTES], key[32];
    size_t n = hex_in(msg_hex, msg, sizeof msg);
    if (n == (size_t)-1) return 0;
    if (hex_in(tag_hex, tag, sizeof tag) != sizeof tag) return 0;
    if (hex_in(key_hex, key, sizeof key) != sizeof key) return 0;
    return ed25519_poly1305_verify(tag, msg, n, key) == 0 ? 1 : 0;
}

/* wrappers: a forma do par (retorno, params) das linhas em bait.def */
static const char* arg_str(NvObject* o) {
    return (o && o->ob_type == NVStr_Type) ? ((NVStr*)o)->value : "";
}

static NvObject* box_bytes(const char* hex) {
    Value out = {NULL};
    create_str(&out, hex ? hex : BAIT_FAIL);
    return out.obj;
}

static NvObject* box_flag(int v) {
    Value out = {NULL};
    create_int(&out, v ? 1 : 0);
    return out.obj;
}

NvObject* nv_bait_mac1_key_builtin(NvObject* a) {
    return box_bytes(nv_bait_mac1_key(arg_str(a)));
}

NvObject* nv_bait_mac1_builtin(NvObject* a, NvObject* b) {
    return box_bytes(nv_bait_mac1(arg_str(a), arg_str(b)));
}

NvObject* nv_bait_mac1_verify_builtin(NvObject* a, NvObject* b, NvObject* c) {
    return box_flag(nv_bait_mac1_verify(arg_str(a), arg_str(b), arg_str(c)));
}
