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
