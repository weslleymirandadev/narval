// crypto_bridge.c — o módulo `crypto` do runtime: as primitivas, uma linha por wrapper.
//
// A cripto em si é o TweetNaCl + o wrapper `ed25519.h` do autor, vendorizados em
// `ed25519/` (domínio público, portátil: getrandom no POSIX, BCryptGenRandom no Windows).
// Este arquivo é só a **fronteira com a linguagem**:
//
//   * bytes cruzam como hex (o mesmo caminho binário do `net`, já testado);
//   * nada de estado global visível: cada primitiva é pura, entra hex e sai hex;
//   * o resultado de falha é "!" — nunca um hex válido, então o `.nv` consegue detectar
//     sem precisar de canal de erro;
//   * as declarações saem de `crypto.def` (modo declaração), os corpos dos wrappers saem
//     das macros de forma abaixo. Uma primitiva nova = 1 linha no .def + 1 linha aqui + o
//     corpo. Nada de "um milhão de return nv_*" espalhado.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/runtime/net_bridge.h"          // NvObject/box helpers vêm de nv_runtime.h
#include "backend/runtime/modules/crypto_decls.h"
#include "ed25519/ed25519.h"

// ── buffers ─────────────────────────────────────────────────────────────────
//
// Entradas vivem numa arena resetada a cada chamada (uma primitiva roda até o fim sem
// chamar outra, então não há aninhamento a proteger) e a saída num buffer crescido sob
// demanda. Nada disso é estado da linguagem: é rascunho da chamada.

static unsigned char* g_arena = NULL;
static size_t g_arena_cap = 0;
static size_t g_arena_used = 0;

static char* g_out = NULL;
static size_t g_out_cap = 0;

#define NV_CRYPTO_FAIL "!"
#define NV_CRYPTO_MAX_BYTES (1u << 20)   // 1 MiB: teto para random() e para um payload

static void arena_reset(void) { g_arena_used = 0; }

static unsigned char* arena_alloc(size_t n) {
    if (n > NV_CRYPTO_MAX_BYTES) return NULL;
    if (g_arena_used + n > g_arena_cap) {
        size_t want = g_arena_used + n + 256;
        unsigned char* p = (unsigned char*)realloc(g_arena, want);
        if (!p) return NULL;
        g_arena = p;
        g_arena_cap = want;
    }
    unsigned char* out = g_arena + g_arena_used;
    g_arena_used += n;
    return out;
}

static char* out_reserve(size_t need) {
    if (need > g_out_cap) {
        size_t want = need < 512 ? 512 : need;
        char* p = (char*)realloc(g_out, want);
        if (!p) return NULL;
        g_out = p;
        g_out_cap = want;
    }
    if (g_out) g_out[0] = '\0';
    return g_out;
}

// ── hex (bytes cruzando a ABI da linguagem) ─────────────────────────────────

static int hx_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char hx_char(int v) { return (char)(v < 10 ? '0' + v : 'a' + (v - 10)); }

// Decode `hex` into the arena. Returns NULL on a malformed string (odd length or a
// non-hex digit) — the callers turn that into NV_CRYPTO_FAIL, never into a silent zero.
static unsigned char* hex_in(const char* hex, size_t* out_len) {
    *out_len = 0;
    if (!hex) return NULL;
    size_t n = strlen(hex);
    if (n % 2 != 0) return NULL;
    size_t bytes = n / 2;
    unsigned char* buf = arena_alloc(bytes ? bytes : 1);
    if (!buf) return NULL;
    for (size_t i = 0; i < bytes; i++) {
        int hi = hx_digit(hex[i * 2]), lo = hx_digit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return NULL;
        buf[i] = (unsigned char)(((hi << 4) | lo) & 0xFF);
    }
    *out_len = bytes;
    return buf;
}

static const char* hex_out(const unsigned char* buf, size_t n) {
    char* out = out_reserve(n * 2 + 1);
    if (!out) return NV_CRYPTO_FAIL;
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = hx_char((buf[i] >> 4) & 0x0F);
        out[i * 2 + 1] = hx_char(buf[i] & 0x0F);
    }
    out[n * 2] = '\0';
    return out;
}

// ── primitivas ──────────────────────────────────────────────────────────────

const char* nv_crypto_random(int nbytes) {
    if (nbytes < 0 || (unsigned)nbytes > NV_CRYPTO_MAX_BYTES) return NV_CRYPTO_FAIL;
    arena_reset();
    unsigned char* buf = arena_alloc((size_t)nbytes ? (size_t)nbytes : 1);
    if (!buf) return NV_CRYPTO_FAIL;
    randombytes(buf, (uint64_t)nbytes);
    return hex_out(buf, (size_t)nbytes);
}

const char* nv_crypto_sha512(const char* msg) {
    arena_reset();
    size_t n = 0;
    unsigned char* in = hex_in(msg, &n);
    if (!in) return NV_CRYPTO_FAIL;
    unsigned char digest[64];
    ed25519_sha512(digest, in, n);
    return hex_out(digest, sizeof(digest));
}

const char* nv_crypto_hmac_sha512(const char* msg, const char* key) {
    arena_reset();
    size_t mn = 0, kn = 0;
    unsigned char* m = hex_in(msg, &mn);
    unsigned char* k = hex_in(key, &kn);
    if (!m || !k) return NV_CRYPTO_FAIL;
    unsigned char mac[64];
    ed25519_hmac_sha512(mac, m, mn, k, kn);
    return hex_out(mac, sizeof(mac));
}

const char* nv_crypto_poly1305(const char* msg, const char* key) {
    arena_reset();
    size_t mn = 0, kn = 0;
    unsigned char* m = hex_in(msg, &mn);
    unsigned char* k = hex_in(key, &kn);
    if (!m || !k || kn != 32) return NV_CRYPTO_FAIL;
    unsigned char tag[16];
    ed25519_poly1305(tag, m, mn, k);
    return hex_out(tag, sizeof(tag));
}

int nv_crypto_poly1305_verify(const char* tag, const char* msg, const char* key) {
    arena_reset();
    size_t tn = 0, mn = 0, kn = 0;
    unsigned char* t = hex_in(tag, &tn);
    unsigned char* m = hex_in(msg, &mn);
    unsigned char* k = hex_in(key, &kn);
    if (!t || !m || !k || tn != 16 || kn != 32) return 0;
    return ed25519_poly1305_verify(t, m, mn, k) == 0 ? 1 : 0;
}

// Ciphertext is message + 32 bytes (TweetNaCl's padded layout), which is what
// secretbox_open expects back.
// WARNING (vendored API pitfall): the ed25519.h wrappers take the MESSAGE length,
// not the ciphertext length — `ed25519_secretbox_open(m, c, n, ...)` calls
// `crypto_secretbox_open(buf, c, n + 32, ...)` internally, and its callers pass
// `blen - 32`. Passing the ciphertext length here made TweetNaCl read 32 extra bytes
// and caused opening to fail with the CORRECT key (the "!" in the round trip) — exactly
// what the vectors caught. The 1024 limit is the wrapper's buffer (`buf[1024 + 32]` when
// opening; encryption has 2048 and fails SILENTLY above that): both sides reject equally
// here, instead of one truncating while the other overflows.
#define NV_CRYPTO_BOX_MAX 1024

const char* nv_crypto_secretbox(const char* msg, const char* nonce, const char* key) {
    arena_reset();
    size_t mn = 0, nn = 0, kn = 0;
    unsigned char* m = hex_in(msg, &mn);
    unsigned char* no = hex_in(nonce, &nn);
    unsigned char* k = hex_in(key, &kn);
    if (!m || !no || !k || nn != 24 || kn != 32) return NV_CRYPTO_FAIL;
    if (mn > NV_CRYPTO_BOX_MAX) return NV_CRYPTO_FAIL;
    unsigned char* c = arena_alloc(mn + 32);
    if (!c) return NV_CRYPTO_FAIL;
    ed25519_secretbox(c, m, mn, no, k);
    return hex_out(c, mn + 32);
}

const char* nv_crypto_secretbox_open(const char* cipher, const char* nonce, const char* key) {
    arena_reset();
    size_t cn = 0, nn = 0, kn = 0;
    unsigned char* c = hex_in(cipher, &cn);
    unsigned char* no = hex_in(nonce, &nn);
    unsigned char* k = hex_in(key, &kn);
    if (!c || !no || !k || nn != 24 || kn != 32 || cn < 32) return NV_CRYPTO_FAIL;
    const size_t mlen = cn - 32;                       // a API dele quer a mensagem, nao o ciphertext
    if (mlen > NV_CRYPTO_BOX_MAX) return NV_CRYPTO_FAIL;
    unsigned char* m = arena_alloc(mlen ? mlen : 1);
    if (!m) return NV_CRYPTO_FAIL;
    if (ed25519_secretbox_open(m, c, mlen, no, k) != 0) return NV_CRYPTO_FAIL;
    return hex_out(m, mlen);
}

const char* nv_crypto_scalarmult(const char* secret, const char* point) {
    arena_reset();
    size_t sn = 0, pn = 0;
    unsigned char* s = hex_in(secret, &sn);
    unsigned char* p = hex_in(point, &pn);
    if (!s || !p || sn != 32 || pn != 32) return NV_CRYPTO_FAIL;
    unsigned char q[32];
    if (ed25519_scalarmult(q, s, p) != 0) return NV_CRYPTO_FAIL;
    return hex_out(q, sizeof(q));
}

const char* nv_crypto_scalarmult_base(const char* secret) {
    arena_reset();
    size_t sn = 0;
    unsigned char* s = hex_in(secret, &sn);
    if (!s || sn != 32) return NV_CRYPTO_FAIL;
    unsigned char q[32];
    if (ed25519_scalarmult_base(q, s) != 0) return NV_CRYPTO_FAIL;
    return hex_out(q, sizeof(q));
}

// The secret key is 64 bytes (seed || pub), but a bare 32-byte seed is accepted and
// completed here — the wrapper reads 64 bytes and must never read past a short buffer.
const char* nv_crypto_sign(const char* msg, const char* sk) {
    arena_reset();
    size_t mn = 0, kn = 0;
    unsigned char* m = hex_in(msg, &mn);
    unsigned char* k = hex_in(sk, &kn);
    if (!m || !k || (kn != 32 && kn < 64)) return NV_CRYPTO_FAIL;

    unsigned char full[64];
    if (kn == 32) {
        if (ed25519_seed_to_pub(full + 32, k) != 0) return NV_CRYPTO_FAIL;
        memcpy(full, k, 32);
    } else {
        memcpy(full, k, 64);
    }
    unsigned char sig[64];
    if (ed25519_sign(sig, m, mn, full) != 0) return NV_CRYPTO_FAIL;
    return hex_out(sig, sizeof(sig));
}

int nv_crypto_verify(const char* msg, const char* sig, const char* pub) {
    arena_reset();
    size_t mn = 0, sn = 0, pn = 0;
    unsigned char* m = hex_in(msg, &mn);
    unsigned char* s = hex_in(sig, &sn);
    unsigned char* p = hex_in(pub, &pn);
    if (!m || !s || !p || sn != 64 || pn != 32) return 0;
    return ed25519_verify(m, mn, s, p) == 0 ? 1 : 0;
}

// sk || pub (64 bytes = 128 hex chars): a identidade inteira num valor só, que o `.nv`
// separa. A seed é o que nunca sai do dispositivo; o pub vai no wire.
const char* nv_crypto_keypair(void) {
    arena_reset();
    unsigned char* buf = arena_alloc(64);
    if (!buf) return NV_CRYPTO_FAIL;
    if (ed25519_keypair(buf, buf + 32) != 0) return NV_CRYPTO_FAIL;
    return hex_out(buf, 64);
}

const char* nv_crypto_seed_to_pub(const char* seed) {
    arena_reset();
    size_t sn = 0;
    unsigned char* s = hex_in(seed, &sn);
    if (!s || sn != 32) return NV_CRYPTO_FAIL;
    unsigned char pub[32];
    if (ed25519_seed_to_pub(pub, s) != 0) return NV_CRYPTO_FAIL;
    return hex_out(pub, sizeof(pub));
}

const char* nv_crypto_pub_to_x25519(const char* ed_pub) {
    arena_reset();
    size_t pn = 0;
    unsigned char* p = hex_in(ed_pub, &pn);
    if (!p || pn != 32) return NV_CRYPTO_FAIL;
    unsigned char out[32];
    ed25519_pub_to_x25519(out, p);
    return hex_out(out, sizeof(out));
}

// ── wrappers para a linguagem (uma linha por primitiva) ─────────────────────
//
// A forma do wrapper é o par (retorno, params) da linha em crypto.def: bytes de entrada
// chegam como str, inteiro como int, e a saída é boxed no tipo do `.def`. É o que faz a
// superfície do usuário ser `crypto.sha512(...)` sem nenhum nv_* à vista.

static const char* arg_str(NvObject* o) {
    return (o && o->ob_type == NVStr_Type) ? ((NVStr*)o)->value : NULL;
}

static int arg_num(NvObject* o) {
    return (o && o->ob_type == NVInt_Type) ? (int)((NVInt*)o)->value : 0;
}

static NvObject* box_bytes(const char* hex) {
    Value out = {NULL};
    create_str(&out, hex ? hex : NV_CRYPTO_FAIL);
    return out.obj;
}

static NvObject* box_flag(int v) {
    Value out = {NULL};
    create_int(&out, v ? 1 : 0);
    return out.obj;
}

#define NVW_B0(name) \
    NvObject* nv_crypto_##name##_builtin(void) { return box_bytes(nv_crypto_##name()); }
#define NVW_B_num(name) \
    NvObject* nv_crypto_##name##_builtin(NvObject* n) { return box_bytes(nv_crypto_##name(arg_num(n))); }
#define NVW_B_b(name) \
    NvObject* nv_crypto_##name##_builtin(NvObject* a) { return box_bytes(nv_crypto_##name(arg_str(a))); }
#define NVW_B_bb(name) \
    NvObject* nv_crypto_##name##_builtin(NvObject* a, NvObject* b) { return box_bytes(nv_crypto_##name(arg_str(a), arg_str(b))); }
#define NVW_B_bbb(name) \
    NvObject* nv_crypto_##name##_builtin(NvObject* a, NvObject* b, NvObject* c) { return box_bytes(nv_crypto_##name(arg_str(a), arg_str(b), arg_str(c))); }
#define NVW_F_bbb(name) \
    NvObject* nv_crypto_##name##_builtin(NvObject* a, NvObject* b, NvObject* c) { return box_flag(nv_crypto_##name(arg_str(a), arg_str(b), arg_str(c))); }

NVW_B_num(random)
NVW_B_b(sha512)
NVW_B_bb(hmac_sha512)
NVW_B_bb(poly1305)
NVW_F_bbb(poly1305_verify)
NVW_B_bbb(secretbox)
NVW_B_bbb(secretbox_open)
NVW_B_bb(scalarmult)
NVW_B_b(scalarmult_base)
NVW_B_bb(sign)
NVW_F_bbb(verify)
NVW_B0(keypair)
NVW_B_b(seed_to_pub)
NVW_B_b(pub_to_x25519)
