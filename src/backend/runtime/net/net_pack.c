// net_pack.c — byte-level packing for the `net` stdlib (Packet and Frame).
//
// A Narval string is NUL-terminated, so binary travels through the language as hex text:
// two lowercase hex digits per byte, no separators. Everything here is pure — a hex string
// in, a value out, no handles and no state to leak — and the byte order is explicit in
// every call, because a wire format always fixes it (the IPv69 wire, for instance, is
// big-endian on the header).
//
// Bounds are the caller's business (Packet tracks the cursor); an out-of-range read returns
// 0 or "" instead of faulting, so a malformed frame degrades into a detectable value.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/runtime/net_bridge.h"

// Result buffer, grown on demand: a frame can be 64 KiB, i.e. 128 KiB of hex.
static char* g_out = NULL;
static size_t g_out_cap = 0;

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

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char hex_char(int v) {
    return (char)(v < 10 ? '0' + v : 'a' + (v - 10));
}

// ── Core ────────────────────────────────────────────────────────────────────

int nv_net_hex_valid(const char* hex) {
    if (!hex) return 0;
    size_t n = strlen(hex);
    if (n % 2 != 0) return 0;
    for (size_t i = 0; i < n; i++)
        if (hex_digit(hex[i]) < 0) return 0;
    return 1;
}

int nv_net_hex_len(const char* hex) {
    if (!hex || !nv_net_hex_valid(hex)) return 0;
    return (int)(strlen(hex) / 2);
}

const char* nv_net_str_to_hex(const char* text) {
    if (!text) return "";
    size_t n = strlen(text);
    char* buf = out_reserve(n * 2 + 1);
    if (!buf) return "";
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        buf[i * 2] = hex_char((c >> 4) & 0x0F);
        buf[i * 2 + 1] = hex_char(c & 0x0F);
    }
    buf[n * 2] = '\0';
    return buf;
}

const char* nv_net_hex_to_str(const char* hex) {
    if (!hex || !nv_net_hex_valid(hex)) return "";
    size_t n = strlen(hex) / 2;
    char* buf = out_reserve(n + 1);
    if (!buf) return "";
    for (size_t i = 0; i < n; i++) {
        int hi = hex_digit(hex[i * 2]), lo = hex_digit(hex[i * 2 + 1]);
        buf[i] = (char)(((hi << 4) | lo) & 0xFF);
    }
    buf[n] = '\0';
    return buf;
}

// Append `nbytes` of `value` in the requested order. Returns the buffer unchanged when the
// width is not 1..8 or the incoming hex is malformed.
const char* nv_net_pack_int(const char* hex, long long value, int nbytes, int le) {
    if (!hex) hex = "";
    if (!nv_net_hex_valid(hex)) return "";
    if (nbytes < 1 || nbytes > 8) return hex;
    unsigned long long v = (unsigned long long)value;
    size_t n = strlen(hex);
    char* buf = out_reserve(n + (size_t)nbytes * 2 + 1);
    if (!buf) return "";
    memcpy(buf, hex, n);
    for (int i = 0; i < nbytes; i++) {
        int shift = le ? i : (nbytes - 1 - i);
        unsigned char byte = (unsigned char)((v >> (shift * 8)) & 0xFF);
        buf[n + (size_t)i * 2] = hex_char((byte >> 4) & 0x0F);
        buf[n + (size_t)i * 2 + 1] = hex_char(byte & 0x0F);
    }
    buf[n + (size_t)nbytes * 2] = '\0';
    return buf;
}

// Read `nbytes` at byte offset `pos` and widen to a signed 64-bit value. Out of range → 0.
long long nv_net_unpack_int(const char* hex, int pos, int nbytes, int le) {
    if (!hex || !nv_net_hex_valid(hex)) return 0;
    if (nbytes < 1 || nbytes > 8 || pos < 0) return 0;
    if ((size_t)(pos + nbytes) > strlen(hex) / 2) return 0;
    unsigned long long v = 0;
    for (int i = 0; i < nbytes; i++) {
        int hi = hex_digit(hex[(size_t)(pos + i) * 2]);
        int lo = hex_digit(hex[(size_t)(pos + i) * 2 + 1]);
        unsigned long long byte = (unsigned long long)(((hi << 4) | lo) & 0xFF);
        int shift = le ? i : (nbytes - 1 - i);
        v |= byte << (shift * 8);
    }
    return (long long)v;
}

// `nbytes` bytes at `pos` as hex text — the binary-safe slice (a payload that contains NULs
// survives this far; turning it into a Narval string truncates at the first NUL).
const char* nv_net_unpack_hex(const char* hex, int pos, int nbytes) {
    if (!hex || !nv_net_hex_valid(hex) || pos < 0 || nbytes < 0) return "";
    size_t len = strlen(hex) / 2;
    if ((size_t)pos + (size_t)nbytes > len) return "";
    char* buf = out_reserve((size_t)nbytes * 2 + 1);
    if (!buf) return "";
    memcpy(buf, hex + (size_t)pos * 2, (size_t)nbytes * 2);
    buf[(size_t)nbytes * 2] = '\0';
    return buf;
}

const char* nv_net_platform_name(void) {
#if defined(_WIN32)
    return "windows";
#elif defined(__linux__)
    return "linux";
#else
    return "unix";
#endif
}

// What this build can actually do, as a csv. `raw` (L2 cru) is compile-time detected instead
// of presumed: it needs AF_PACKET on Linux and Npcap on Windows, neither of which is in the
// build yet, so it is reported only where the kernel interface exists.
const char* nv_net_caps(void) {
#if defined(_WIN32)
    return "tcp,udp,dns,poll";
#elif defined(AF_PACKET)
    return "tcp,udp,dns,poll";
#else
    return "tcp,udp,dns,poll";
#endif
}

// ── Narval-facing wrappers ──────────────────────────────────────────────────

static const char* arg_str(NvObject* o) {
    return (o && o->ob_type == NVStr_Type) ? ((NVStr*)o)->value : NULL;
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

NvObject* nv_net_hex_valid_builtin(NvObject* hex) {
    return box_int(nv_net_hex_valid(arg_str(hex)));
}

NvObject* nv_net_hex_len_builtin(NvObject* hex) {
    return box_int(nv_net_hex_len(arg_str(hex)));
}

NvObject* nv_net_str_to_hex_builtin(NvObject* text) {
    return box_str(nv_net_str_to_hex(arg_str(text)));
}

NvObject* nv_net_hex_to_str_builtin(NvObject* hex) {
    return box_str(nv_net_hex_to_str(arg_str(hex)));
}

NvObject* nv_net_pack_int_builtin(NvObject* hex, NvObject* value, NvObject* nbytes,
                                  NvObject* le) {
    return box_str(nv_net_pack_int(arg_str(hex), arg_int(value, 0), (int)arg_int(nbytes, 0),
                                   (int)arg_int(le, 0)));
}

NvObject* nv_net_unpack_int_builtin(NvObject* hex, NvObject* pos, NvObject* nbytes,
                                    NvObject* le) {
    return box_int(nv_net_unpack_int(arg_str(hex), (int)arg_int(pos, -1),
                                     (int)arg_int(nbytes, 0), (int)arg_int(le, 0)));
}

NvObject* nv_net_unpack_hex_builtin(NvObject* hex, NvObject* pos, NvObject* nbytes) {
    return box_str(nv_net_unpack_hex(arg_str(hex), (int)arg_int(pos, -1),
                                     (int)arg_int(nbytes, -1)));
}

NvObject* nv_net_platform_builtin(void) { return box_str(nv_net_platform_name()); }

NvObject* nv_net_caps_builtin(void) { return box_str(nv_net_caps()); }
