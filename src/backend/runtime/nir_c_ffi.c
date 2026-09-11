// nir_c_ffi.c — C standard library FFI bridges for the NIR pipeline.
//
// These wrappers expose narval-ABI (NvObject* in/out) versions of C standard
// library functions listed in ffi.cpp's get_c_registry().
// The NIR extern_stmt codegen declares nv_ffi_<name> as external and emits a
// thin local wrapper function @<user_name> that forwards to the bridge.

#include "backend/runtime/nv_runtime.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

extern NvTypeObject* NVInt_Type;
extern NvTypeObject* NVFloat_Type;
extern NvTypeObject* NVBool_Type;
extern NvTypeObject* NVStr_Type;

// ── Helpers ──────────────────────────────────────────────────────────────────

static double obj_to_d(NvObject* o) {
    if (!o) return 0.0;
    if (o->ob_type == NVFloat_Type) return ((NVFloat*)o)->value;
    if (o->ob_type == NVInt_Type)   return (double)((NVInt*)o)->value;
    if (o->ob_type == NVBool_Type)  return (double)((NVBool*)o)->value;
    return 0.0;
}

static int32_t obj_to_i(NvObject* o) {
    if (!o) return 0;
    if (o->ob_type == NVInt_Type)   return ((NVInt*)o)->value;
    if (o->ob_type == NVFloat_Type) return (int32_t)((NVFloat*)o)->value;
    if (o->ob_type == NVBool_Type)  return ((NVBool*)o)->value;
    return 0;
}

static const char* obj_to_s(NvObject* o) {
    if (!o) return "";
    if (o->ob_type == NVStr_Type) {
        const char* s = ((NVStr*)o)->value;
        return s ? s : "";
    }
    return "";
}

static NvObject* box_d(double v) {
    Value out = {NULL};
    create_float(&out, v);
    return out.obj;
}

static NvObject* box_i(int32_t v) {
    Value out = {NULL};
    create_int(&out, v);
    return out.obj;
}

static NvObject* box_s(const char* v) {
    Value out = {NULL};
    create_str(&out, v ? v : "");
    return out.obj;
}

static NvObject* box_none(void) {
    Value out = {NULL};
    create_option_none(&out);
    return out.obj;
}

// ── Math library ─────────────────────────────────────────────────────────────

NvObject* nv_ffi_sin(NvObject* x)                         { return box_d(sin(obj_to_d(x))); }
NvObject* nv_ffi_cos(NvObject* x)                         { return box_d(cos(obj_to_d(x))); }
NvObject* nv_ffi_tan(NvObject* x)                         { return box_d(tan(obj_to_d(x))); }
NvObject* nv_ffi_asin(NvObject* x)                        { return box_d(asin(obj_to_d(x))); }
NvObject* nv_ffi_acos(NvObject* x)                        { return box_d(acos(obj_to_d(x))); }
NvObject* nv_ffi_atan(NvObject* x)                        { return box_d(atan(obj_to_d(x))); }
NvObject* nv_ffi_atan2(NvObject* y, NvObject* x)          { return box_d(atan2(obj_to_d(y), obj_to_d(x))); }
NvObject* nv_ffi_sqrt(NvObject* x)                        { return box_d(sqrt(obj_to_d(x))); }
NvObject* nv_ffi_cbrt(NvObject* x)                        { return box_d(cbrt(obj_to_d(x))); }
NvObject* nv_ffi_pow(NvObject* base, NvObject* exponent)  { return box_d(pow(obj_to_d(base), obj_to_d(exponent))); }
NvObject* nv_ffi_exp(NvObject* x)                         { return box_d(exp(obj_to_d(x))); }
NvObject* nv_ffi_log(NvObject* x)                         { return box_d(log(obj_to_d(x))); }
NvObject* nv_ffi_log10(NvObject* x)                       { return box_d(log10(obj_to_d(x))); }
NvObject* nv_ffi_log2(NvObject* x)                        { return box_d(log2(obj_to_d(x))); }
NvObject* nv_ffi_floor(NvObject* x)                       { return box_d(floor(obj_to_d(x))); }
NvObject* nv_ffi_ceil(NvObject* x)                        { return box_d(ceil(obj_to_d(x))); }
NvObject* nv_ffi_round(NvObject* x)                       { return box_d(round(obj_to_d(x))); }
NvObject* nv_ffi_fabs(NvObject* x)                        { return box_d(fabs(obj_to_d(x))); }
NvObject* nv_ffi_fmod(NvObject* x, NvObject* y)           { return box_d(fmod(obj_to_d(x), obj_to_d(y))); }
NvObject* nv_ffi_hypot(NvObject* x, NvObject* y)          { return box_d(hypot(obj_to_d(x), obj_to_d(y))); }

// ── Stdlib library ───────────────────────────────────────────────────────────

NvObject* nv_ffi_rand(void)                               { return box_i((int32_t)rand()); }
NvObject* nv_ffi_srand(NvObject* seed)                    { srand((unsigned int)obj_to_i(seed)); return box_none(); }
NvObject* nv_ffi_abs(NvObject* x)                         { return box_i(abs(obj_to_i(x))); }
NvObject* nv_ffi_atoi(NvObject* s)                        { return box_i((int32_t)atoi(obj_to_s(s))); }
NvObject* nv_ffi_atof(NvObject* s)                        { return box_d(atof(obj_to_s(s))); }
NvObject* nv_ffi_exit(NvObject* code)                     { exit(obj_to_i(code)); return box_none(); }
NvObject* nv_ffi_malloc(NvObject* size)                   { return box_i((int32_t)(intptr_t)malloc((size_t)(size_t)obj_to_i(size))); }
NvObject* nv_ffi_free(NvObject* ptr)                      { free((void*)(intptr_t)obj_to_i(ptr)); return box_none(); }

// ── String library ───────────────────────────────────────────────────────────

NvObject* nv_ffi_strlen(NvObject* s)                      { return box_i((int32_t)strlen(obj_to_s(s))); }
NvObject* nv_ffi_strcmp(NvObject* a, NvObject* b)         { return box_i((int32_t)strcmp(obj_to_s(a), obj_to_s(b))); }
NvObject* nv_ffi_strncmp(NvObject* a, NvObject* b, NvObject* n) {
    return box_i((int32_t)strncmp(obj_to_s(a), obj_to_s(b), (size_t)obj_to_i(n)));
}
NvObject* nv_ffi_strchr(NvObject* s, NvObject* c) {
    const char* r = strchr(obj_to_s(s), (int)obj_to_i(c));
    return r ? box_s(r) : box_none();
}
NvObject* nv_ffi_strstr(NvObject* haystack, NvObject* needle) {
    const char* r = strstr(obj_to_s(haystack), obj_to_s(needle));
    return r ? box_s(r) : box_none();
}

// ── Stdio library ────────────────────────────────────────────────────────────

NvObject* nv_ffi_printf(NvObject* fmt)                    { return box_i((int32_t)printf("%s", obj_to_s(fmt))); }
NvObject* nv_ffi_puts(NvObject* s)                        { return box_i((int32_t)puts(obj_to_s(s))); }
NvObject* nv_ffi_putchar(NvObject* c)                     { return box_i((int32_t)putchar((int)obj_to_i(c))); }
NvObject* nv_ffi_getchar(void)                            { return box_i((int32_t)getchar()); }

// ── Time library ─────────────────────────────────────────────────────────────

NvObject* nv_ffi_time(NvObject* t)                        { (void)t; return box_i((int32_t)time(NULL)); }
NvObject* nv_ffi_clock(void)                              { return box_i((int32_t)clock()); }

// ── Generic dlsym bridges (comptime import_c) ────────────────────────────────
// One bridge per signature shape, encoded as <ret><params> with d = double,
// i = int32, s = char*, v = void. The first argument is the C symbol name as a
// boxed string; the runtime resolves it with dlsym at call time, so
// `comptime import_c("math.h")` does not need a hand-written wrapper per
// function (unlike the fixed nv_ffi_<name> registry above).
#include <dlfcn.h>

static void* cimp_sym(NvObject* name_obj) {
    const char* name = obj_to_s(name_obj);
    void* p = dlsym(RTLD_DEFAULT, name);
    if (!p)
        fprintf(stderr,
                "narval: import_c: symbol '%s' not found (library not linked? "
                "pass link: \"name\" to import_c)\n", name);
    return p;
}

NvObject* nv_ffi_call_v(NvObject* n) {
    void (*f)(void) = (void (*)(void))cimp_sym(n);
    if (!f) return box_none();
    f();
    return box_none();
}

NvObject* nv_ffi_call_i(NvObject* n) {
    int32_t (*f)(void) = (int32_t (*)(void))cimp_sym(n);
    return box_i(f ? f() : 0);
}

NvObject* nv_ffi_call_d(NvObject* n) {
    double (*f)(void) = (double (*)(void))cimp_sym(n);
    return box_d(f ? f() : 0.0);
}

NvObject* nv_ffi_call_ii(NvObject* n, NvObject* a) {
    int32_t (*f)(int32_t) = (int32_t (*)(int32_t))cimp_sym(n);
    return box_i(f ? f(obj_to_i(a)) : 0);
}

NvObject* nv_ffi_call_dd(NvObject* n, NvObject* a) {
    double (*f)(double) = (double (*)(double))cimp_sym(n);
    return box_d(f ? f(obj_to_d(a)) : 0.0);
}

NvObject* nv_ffi_call_di(NvObject* n, NvObject* a) {
    double (*f)(int32_t) = (double (*)(int32_t))cimp_sym(n);
    return box_d(f ? f(obj_to_i(a)) : 0.0);
}

NvObject* nv_ffi_call_id(NvObject* n, NvObject* a) {
    int32_t (*f)(double) = (int32_t (*)(double))cimp_sym(n);
    return box_i(f ? f(obj_to_d(a)) : 0);
}

NvObject* nv_ffi_call_iii(NvObject* n, NvObject* a, NvObject* b) {
    int32_t (*f)(int32_t, int32_t) = (int32_t (*)(int32_t, int32_t))cimp_sym(n);
    return box_i(f ? f(obj_to_i(a), obj_to_i(b)) : 0);
}

NvObject* nv_ffi_call_ddd(NvObject* n, NvObject* a, NvObject* b) {
    double (*f)(double, double) = (double (*)(double, double))cimp_sym(n);
    return box_d(f ? f(obj_to_d(a), obj_to_d(b)) : 0.0);
}

NvObject* nv_ffi_call_is(NvObject* n, NvObject* a) {
    int32_t (*f)(const char*) = (int32_t (*)(const char*))cimp_sym(n);
    return box_i(f ? f(obj_to_s(a)) : 0);
}

NvObject* nv_ffi_call_ss(NvObject* n, NvObject* a) {
    const char* (*f)(const char*) = (const char* (*)(const char*))cimp_sym(n);
    return box_s(f ? f(obj_to_s(a)) : "");
}

NvObject* nv_ffi_call_vs(NvObject* n, NvObject* a) {
    void (*f)(const char*) = (void (*)(const char*))cimp_sym(n);
    if (f) f(obj_to_s(a));
    return box_none();
}

NvObject* nv_ffi_call_ds(NvObject* n, NvObject* a) {
    double (*f)(const char*) = (double (*)(const char*))cimp_sym(n);
    return box_d(f ? f(obj_to_s(a)) : 0.0);
}

// Signatures added for shims shipped with the runtime: a string return, and the
// (int, char*) combinations a handle-based C API needs.
NvObject* nv_ffi_call_s(NvObject* n) {
    const char* (*f)(void) = (const char* (*)(void))cimp_sym(n);
    return box_s(f ? f() : "");
}

NvObject* nv_ffi_call_si(NvObject* n, NvObject* a) {
    const char* (*f)(int32_t) = (const char* (*)(int32_t))cimp_sym(n);
    return box_s(f ? f(obj_to_i(a)) : "");
}

NvObject* nv_ffi_call_iis(NvObject* n, NvObject* a, NvObject* b) {
    int32_t (*f)(int32_t, const char*) = (int32_t (*)(int32_t, const char*))cimp_sym(n);
    return box_i(f ? f(obj_to_i(a), obj_to_s(b)) : 0);
}

NvObject* nv_ffi_call_sis(NvObject* n, NvObject* a, NvObject* b) {
    const char* (*f)(int32_t, const char*) = (const char* (*)(int32_t, const char*))cimp_sym(n);
    return box_s(f ? f(obj_to_i(a), obj_to_s(b)) : "");
}

// ── REPL value store ────────────────────────
// Every REPL input is JIT'd into a fresh JIT, so a variable's value cannot stay in
// a register between lines. The codegen emits nv_repl_set / nv_repl_get for the
// names assigned at the top level of the REPL, and the store keeps them alive.
#define NV_REPL_SLOTS 64

static struct {
    char* name;
    NvObject* value;
} g_repl_slots[NV_REPL_SLOTS];
static int g_repl_used = 0;

static int repl_slot(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < g_repl_used; ++i) {
        if (g_repl_slots[i].name && strcmp(g_repl_slots[i].name, name) == 0) return i;
    }
    return -1;
}

// Value of a REPL variable (none when it was never assigned).
NvObject* nv_repl_get(NvObject* name_obj) {
    const int i = repl_slot(obj_to_s(name_obj));
    if (i < 0) return box_none();
    nv_incref(g_repl_slots[i].value);
    return g_repl_slots[i].value;
}

// Stores a REPL variable and returns the value, so an assignment keeps a value.
NvObject* nv_repl_set(NvObject* name_obj, NvObject* value) {
    const char* name = obj_to_s(name_obj);
    if (!name || !value) return box_none();
    int i = repl_slot(name);
    if (i < 0) {
        if (g_repl_used >= NV_REPL_SLOTS) return box_none();
        i = g_repl_used++;
        g_repl_slots[i].name = strdup(name);
        g_repl_slots[i].value = NULL;
    }
    nv_incref(value);                      // the store holds one reference
    nv_incref(value);                      // and the assignment keeps one
    nv_decref(g_repl_slots[i].value);      // release whatever was there
    g_repl_slots[i].value = value;
    return value;
}
