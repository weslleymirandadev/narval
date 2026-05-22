/*
 * runtime_nostd.c — Minimal freestanding runtime for @[no_std] builds.
 *
 * No libc dependency: no malloc, no printf, no strlen from CRT.
 * Provides the minimum set of functions the Narval codegen emits when
 * @[no_std] is active. Compiled with -ffunction-sections so --gc-sections
 * dead-strips unused functions from the final binary.
 *
 * Memory model: fixed-size static bump allocator (NV_NS_HEAP_SIZE bytes,
 * default 64 KB). Objects are never freed — suitable for OS/kernel/
 * embedded programs where a simple arena lifetime is acceptable.
 */

#include "backend/runtime/prototypes.h"

/*  Build-time configurable heap size  */
#ifndef NV_NS_HEAP_SIZE
# define NV_NS_HEAP_SIZE 65536
#endif

/*  Freestanding helpers (no <string.h> / <stdlib.h>)  */

static size_t _ns_strlen(const char* s) {
    size_t n = 0;
    if (s) while (s[n]) ++n;
    return n;
}

static int _ns_strcmp(const char* a, const char* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return  1;
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

/*  Static bump allocator  */

static char   _nv_ns_heap[NV_NS_HEAP_SIZE];
static size_t _nv_ns_top = 0;

static void* _nv_ns_alloc(size_t n) {
    n = (n + 7u) & ~7u;
    if (_nv_ns_top + n > NV_NS_HEAP_SIZE) return (void*)0;
    void* p = (void*)(_nv_ns_heap + _nv_ns_top);
    _nv_ns_top += n;
    /* zero-initialise the returned block */
    char* b = (char*)p;
    for (size_t i = 0; i < n; ++i) b[i] = 0;
    return p;
}

/*  Minimal static type objects 
 * These live in BSS (zero-initialised at load time).
 * tp_name is the only field that needs a value for basic operation;
 * the rest (methods, bases, etc.) stay NULL / 0.
 */

static NvTypeObject _ns_int_type;
static NvTypeObject _ns_float_type;
static NvTypeObject _ns_bool_type;
static NvTypeObject _ns_char_type;
static NvTypeObject _ns_str_type;

/*  Global type pointers — definitions (override nv_runtime.c)  */

NvTypeObject* NVInt_Type    = (NvTypeObject*)0;
NvTypeObject* NVFloat_Type  = (NvTypeObject*)0;
NvTypeObject* NVBool_Type   = (NvTypeObject*)0;
NvTypeObject* NVChar_Type   = (NvTypeObject*)0;
NvTypeObject* NVStr_Type    = (NvTypeObject*)0;
NvTypeObject* NVArray_Type  = (NvTypeObject*)0;
NvTypeObject* NVVector_Type = (NvTypeObject*)0;
NvTypeObject* NVMap_Type    = (NvTypeObject*)0;
NvTypeObject* NVTuple_Type  = (NvTypeObject*)0;
NvTypeObject* NVObject_Type = (NvTypeObject*)0;
NvTypeObject* NVType_Type   = (NvTypeObject*)0;
NvTypeObject* NVOptionNone_Type = (NvTypeObject*)0;
NvTypeObject* NVOptionSome_Type = (NvTypeObject*)0;
NvTypeObject* NVResultOk_Type   = (NvTypeObject*)0;
NvTypeObject* NVResultErr_Type  = (NvTypeObject*)0;

/* Called once from _narval_ns_start before user code runs. */
void nv_ns_init_types(void) {
    _ns_int_type.tp_name   = "int";
    _ns_float_type.tp_name = "float";
    _ns_bool_type.tp_name  = "bool";
    _ns_char_type.tp_name  = "char";
    _ns_str_type.tp_name   = "str";

    NVInt_Type   = &_ns_int_type;
    NVFloat_Type = &_ns_float_type;
    NVBool_Type  = &_ns_bool_type;
    NVChar_Type  = &_ns_char_type;
    NVStr_Type   = &_ns_str_type;
}

/*  Type query  */

int32_t get_value_type(const Value* v) {
    if (!v || !v->obj) return 0;
    NvTypeObject* t = v->obj->ob_type;
    if (!t) return 0;
    if (t == NVInt_Type)   return NV_INT_BASE;
    if (t == NVFloat_Type) return NV_FLOAT_BASE;
    if (t == NVBool_Type)  return NV_BOOL_BASE;
    if (t == NVChar_Type)  return NV_CHAR_BASE;
    if (t == NVStr_Type)   return NV_STR_BASE;
    return NV_ANY_BASE;
}

/*  Object creation  */

void create_int(Value* out, int32_t value) {
    if (!out) return;
    out->obj = (NvObject*)0;
    NVInt* obj = (NVInt*)_nv_ns_alloc(sizeof(NVInt));
    if (!obj) return;
    obj->ob_base.ob_type   = NVInt_Type;
    obj->ob_base.ref_count = 1;
    obj->ob_base.flags     = 0;
    obj->value = value;
    out->obj = (NvObject*)obj;
}

void create_float(Value* out, double value) {
    if (!out) return;
    out->obj = (NvObject*)0;
    NVFloat* obj = (NVFloat*)_nv_ns_alloc(sizeof(NVFloat));
    if (!obj) return;
    obj->ob_base.ob_type   = NVFloat_Type;
    obj->ob_base.ref_count = 1;
    obj->ob_base.flags     = 0;
    obj->value = value;
    out->obj = (NvObject*)obj;
}

void create_bool(Value* out, int32_t value) {
    if (!out) return;
    out->obj = (NvObject*)0;
    NVBool* obj = (NVBool*)_nv_ns_alloc(sizeof(NVBool));
    if (!obj) return;
    obj->ob_base.ob_type   = NVBool_Type;
    obj->ob_base.ref_count = 1;
    obj->ob_base.flags     = 0;
    obj->value = value ? 1 : 0;
    out->obj = (NvObject*)obj;
}

void create_char(Value* out, char value) {
    if (!out) return;
    out->obj = (NvObject*)0;
    NVChar* obj = (NVChar*)_nv_ns_alloc(sizeof(NVChar));
    if (!obj) return;
    obj->ob_base.ob_type   = NVChar_Type;
    obj->ob_base.ref_count = 1;
    obj->ob_base.flags     = 0;
    obj->value = value;
    out->obj = (NvObject*)obj;
}

/* Stores a pointer to the string literal — no copy, no malloc. */
void create_str(Value* out, const char* value) {
    if (!out) return;
    out->obj = (NvObject*)0;
    NVStr* obj = (NVStr*)_nv_ns_alloc(sizeof(NVStr));
    if (!obj) return;
    obj->ob_base.ob_type   = NVStr_Type;
    obj->ob_base.ref_count = 1;
    obj->ob_base.flags     = 0;
    obj->value = (char*)value;
    obj->len   = (int32_t)_ns_strlen(value);
    out->obj = (NvObject*)obj;
}

/*  Arithmetic  */

static double _ns_extract_num(const Value* v, int32_t* is_float) {
    if (!v || !v->obj) { *is_float = 0; return 0.0; }
    NvTypeObject* t = v->obj->ob_type;
    if (t == NVFloat_Type) { *is_float = 1; return ((NVFloat*)v->obj)->value; }
    if (t == NVInt_Type)   { *is_float = 0; return (double)((NVInt*)v->obj)->value; }
    if (t == NVBool_Type)  { *is_float = 0; return (double)((NVBool*)v->obj)->value; }
    *is_float = 0; return 0.0;
}

void nv_value_add(Value* out, Value* a, Value* b) {
    if (!out) return;
    int fa = 0, fb = 0;
    double va = _ns_extract_num(a, &fa);
    double vb = _ns_extract_num(b, &fb);
    /* String concat is not supported in no_std — checker blocks write anyway */
    if (fa || fb) { create_float(out, va + vb); }
    else          { create_int  (out, (int32_t)(va + vb)); }
}

void nv_value_sub(Value* out, Value* a, Value* b) {
    if (!out) return;
    int fa = 0, fb = 0;
    double va = _ns_extract_num(a, &fa);
    double vb = _ns_extract_num(b, &fb);
    if (fa || fb) { create_float(out, va - vb); }
    else          { create_int  (out, (int32_t)(va - vb)); }
}

void nv_value_mul(Value* out, Value* a, Value* b) {
    if (!out) return;
    int fa = 0, fb = 0;
    double va = _ns_extract_num(a, &fa);
    double vb = _ns_extract_num(b, &fb);
    if (fa || fb) { create_float(out, va * vb); }
    else          { create_int  (out, (int32_t)(va * vb)); }
}

void nv_value_div(Value* out, Value* a, Value* b) {
    if (!out) return;
    int fa = 0, fb = 0;
    double va = _ns_extract_num(a, &fa);
    double vb = _ns_extract_num(b, &fb);
    if (vb == 0.0) { out->obj = (NvObject*)0; return; }
    if (fa || fb) { create_float(out, va / vb); }
    else          { create_int  (out, (int32_t)(va / vb)); }
}

void nv_value_mod(Value* out, Value* a, Value* b) {
    if (!out) return;
    /* Integer-only modulo in no_std (no fmod without libm). */
    if (a && b && a->obj && b->obj) {
        NvTypeObject* ta = a->obj->ob_type;
        NvTypeObject* tb = b->obj->ob_type;
        if (ta == NVInt_Type && tb == NVInt_Type) {
            int32_t va = ((NVInt*)a->obj)->value;
            int32_t vb = ((NVInt*)b->obj)->value;
            if (vb != 0) { create_int(out, va % vb); return; }
        }
    }
    out->obj = (NvObject*)0;
}

/*  Comparison  */

int32_t nv_value_cmp(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return 0;
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (!ta || !tb) return 0;

    if (ta == NVInt_Type && tb == NVInt_Type) {
        int32_t va = ((NVInt*)a->obj)->value;
        int32_t vb = ((NVInt*)b->obj)->value;
        return (va > vb) - (va < vb);
    }
    if (ta == NVBool_Type && tb == NVBool_Type) {
        int32_t va = ((NVBool*)a->obj)->value;
        int32_t vb = ((NVBool*)b->obj)->value;
        return (va > vb) - (va < vb);
    }
    if (ta == NVChar_Type && tb == NVChar_Type) {
        unsigned char va = (unsigned char)((NVChar*)a->obj)->value;
        unsigned char vb = (unsigned char)((NVChar*)b->obj)->value;
        return (va > vb) - (va < vb);
    }
    if (ta == NVStr_Type && tb == NVStr_Type) {
        const char* sa = ((NVStr*)a->obj)->value;
        const char* sb = ((NVStr*)b->obj)->value;
        return _ns_strcmp(sa, sb);
    }
    /* Mixed numeric */
    {
        int fa = 0, fb = 0;
        double va = _ns_extract_num(a, &fa);
        double vb = _ns_extract_num(b, &fb);
        if (fa || fb) {
            if (va < vb) return -1;
            if (va > vb) return  1;
            return 0;
        }
    }
    return _ns_strcmp(ta->tp_name ? ta->tp_name : "",
                      tb->tp_name ? tb->tp_name : "");
}

/*  Bool conversion (used by if/while conditions)  */

void nv_bool_convert(Value* out, Value* input) {
    if (!out) return;
    if (!input || !input->obj) { create_bool(out, 0); return; }
    NvTypeObject* t = input->obj->ob_type;
    if (t == NVInt_Type)  { create_bool(out, ((NVInt*)input->obj)->value != 0); return; }
    if (t == NVFloat_Type){ create_bool(out, ((NVFloat*)input->obj)->value != 0.0); return; }
    if (t == NVBool_Type) { create_bool(out, ((NVBool*)input->obj)->value); return; }
    if (t == NVChar_Type) { create_bool(out, ((NVChar*)input->obj)->value != '\0'); return; }
    if (t == NVStr_Type)  {
        const char* s = ((NVStr*)input->obj)->value;
        create_bool(out, s && s[0] != '\0');
        return;
    }
    create_bool(out, input->obj != (NvObject*)0);
}

/*  Value extraction (for return values / exit codes)  */

int32_t extract_int_from_value(Value* v) {
    if (!v || !v->obj) return 0;
    NvTypeObject* t = v->obj->ob_type;
    if (t == NVInt_Type)   return ((NVInt*)v->obj)->value;
    if (t == NVBool_Type)  return ((NVBool*)v->obj)->value ? 1 : 0;
    if (t == NVChar_Type)  return (int32_t)(unsigned char)((NVChar*)v->obj)->value;
    if (t == NVFloat_Type) return (int32_t)((NVFloat*)v->obj)->value;
    return 0;
}

double extract_float_from_value(Value* v) {
    if (!v || !v->obj) return 0.0;
    NvTypeObject* t = v->obj->ob_type;
    if (t == NVFloat_Type) return ((NVFloat*)v->obj)->value;
    if (t == NVInt_Type)   return (double)((NVInt*)v->obj)->value;
    if (t == NVBool_Type)  return (double)((NVBool*)v->obj)->value;
    if (t == NVChar_Type)  return (double)(unsigned char)((NVChar*)v->obj)->value;
    return 0.0;
}

char* extract_string_from_value(Value* v) {
    if (!v || !v->obj) return (char*)"";
    if (v->obj->ob_type == NVStr_Type)
        return ((NVStr*)v->obj)->value ? ((NVStr*)v->obj)->value : (char*)"";
    return (char*)"";
}

/*  Stubs for codegen symbols not valid in no_std  */
/* The checker blocks these, but the codegen may still reference the   */
/* symbol names. Providing weak no-op stubs prevents linker errors.   */

void nv_push_frame(const char* f, const char* fn) { (void)f; (void)fn; }
void nv_pop_frame(void) {}
void nv_set_line(int32_t l) { (void)l; }

/* ensure_value_type is a no-op in no_std — types are always set. */
void ensure_value_type(Value* v) { (void)v; }

/* nv_decref / nv_incref: bump allocator never frees, so these are no-ops. */
void nv_decref_impl(NvObject* obj) { (void)obj; }
void nv_incref_impl(NvObject* obj) { (void)obj; }
