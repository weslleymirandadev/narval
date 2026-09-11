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

// no_std runtime does not manage object lifetimes.
void nv_drop(NvObject* obj) { (void)obj; }

/* ── Syscalls: the only way in/out without libc ──────────────────────────── */

static long _ns_syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a), "S"(b), "d"(c)
                     : "rcx", "r11", "memory");
    return ret;
}

static void _ns_write_bytes(const char* buf, size_t len) {
    /* 1 = write(2); stdout only, so the fd is a constant */
    _ns_syscall3(1, 1, (long)buf, (long)len);
}

/* Used by the codegen for @[no_std] programs: exit_group(2). The codegen passes a
 * boxed value (it works in Narval values), so the status is extracted here. */
void _exit(NvObject* value) {
    int code = 0;
    if (value && value->ob_type == NVInt_Type) code = ((NVInt*)value)->value;
    _ns_syscall3(231, code, 0, 0);
    for (;;) { }   /* exit_group does not return */
}

/* ── Minimal formatting (printf's %d / %f / %s, which libc would give) ───── */

static void _ns_put_str(const char* s) {
    if (s) _ns_write_bytes(s, _ns_strlen(s));
}

static void _ns_put_int(long v) {
    char buf[24];
    int i = (int)sizeof(buf);
    int neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1UL : (unsigned long)v;
    if (u == 0) buf[--i] = '0';
    while (u) { buf[--i] = (char)('0' + (u % 10)); u /= 10; }
    if (neg) buf[--i] = '-';
    _ns_write_bytes(buf + i, (size_t)((int)sizeof(buf) - i));
}

/* Six decimals, like printf("%f"). */
static void _ns_put_float(double d) {
    if (d != d) { _ns_put_str("nan"); return; }
    if (d < 0) { _ns_put_str("-"); d = -d; }
    unsigned long ip = (unsigned long)d;
    double frac = d - (double)ip;
    unsigned long fp = (unsigned long)(frac * 1000000.0 + 0.5);
    if (fp >= 1000000UL) { ip += 1; fp -= 1000000UL; }
    _ns_put_int((long)ip);
    _ns_put_str(".");
    char buf[6];
    for (int i = 5; i >= 0; --i) { buf[i] = (char)('0' + (fp % 10)); fp /= 10; }
    _ns_write_bytes(buf, 6);
}

/* ── The codegen-facing API (same names the std runtime exposes) ─────────── */

static int _ns_types_ready = 0;

static NvTypeObject _ns_vector_type;
static NvTypeObject _ns_array_type;
static NvTypeObject _ns_none_type;

static void _ns_ensure_types(void) {
    if (_ns_types_ready) return;
    _ns_types_ready = 1;
    nv_ns_init_types();
    _ns_vector_type.tp_name = "vector"; NVVector_Type = &_ns_vector_type;
    _ns_array_type.tp_name  = "array";  NVArray_Type  = &_ns_array_type;
    _ns_none_type.tp_name   = "None";   NVOptionNone_Type = &_ns_none_type;
}

NvObject* nv_box_int(int64_t v) {
    _ns_ensure_types();
    Value out = {0};
    create_int(&out, (int32_t)v);
    return out.obj;
}

NvObject* nv_box_float(double v) {
    _ns_ensure_types();
    Value out = {0};
    create_float(&out, v);
    return out.obj;
}

NvObject* nv_box_bool(int64_t v) {
    _ns_ensure_types();
    Value out = {0};
    create_bool(&out, v != 0 ? 1 : 0);
    return out.obj;
}

NvObject* nv_box_str(const char* s) {
    _ns_ensure_types();
    Value out = {0};
    create_str(&out, s ? s : "");
    return out.obj;
}

NvObject* nv_add(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {0};
    nv_value_add(&out, &va, &vb);
    return out.obj;
}

NvObject* nv_sub(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {0};
    nv_value_sub(&out, &va, &vb);
    return out.obj;
}

NvObject* nv_mul(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {0};
    nv_value_mul(&out, &va, &vb);
    return out.obj;
}

NvObject* nv_div(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {0};
    nv_value_div(&out, &va, &vb);
    return out.obj;
}

NvObject* nv_mod(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {0};
    nv_value_mod(&out, &va, &vb);
    return out.obj;
}

/* Comparisons are boxed booleans, like the std runtime's. */
static NvObject* _ns_cmp_result(NvObject* a, NvObject* b, int want) {
    _ns_ensure_types();
    Value va = {a}, vb = {b};
    const int32_t c = nv_value_cmp(&va, &vb);
    Value out = {0};
    int truth = (want < 0) ? (c < 0) : (want > 0) ? (c > 0) : (c == 0);
    create_bool(&out, truth);
    return out.obj;
}

NvObject* nv_value_lt(NvObject* a, NvObject* b) { return _ns_cmp_result(a, b, -1); }
NvObject* nv_value_gt(NvObject* a, NvObject* b) { return _ns_cmp_result(a, b,  1); }
NvObject* nv_value_le(NvObject* a, NvObject* b) { return _ns_cmp_result(a, b, -1); }
NvObject* nv_value_ge(NvObject* a, NvObject* b) { return _ns_cmp_result(a, b,  1); }
NvObject* nv_value_eq(NvObject* a, NvObject* b) { return _ns_cmp_result(a, b,  0); }
NvObject* nv_value_ne(NvObject* a, NvObject* b) {
    _ns_ensure_types();
    Value va = {a}, vb = {b};
    Value out = {0};
    create_bool(&out, nv_value_cmp(&va, &vb) != 0);
    return out.obj;
}

int nv_value_is_truthy(NvObject* obj) {
    if (!obj || !obj->ob_type) return 0;
    if (obj->ob_type == NVInt_Type)   return ((NVInt*)obj)->value != 0;
    if (obj->ob_type == NVBool_Type)  return ((NVBool*)obj)->value != 0;
    if (obj->ob_type == NVFloat_Type) return ((NVFloat*)obj)->value != 0.0;
    if (obj->ob_type == NVStr_Type)   return ((NVStr*)obj)->value && ((NVStr*)obj)->value[0] != 0;
    return 1;
}

NvObject* nv_select(int cond, NvObject* a, NvObject* b) {
    return cond ? a : b;
}

/* ── Output ──────────────────────────────────────────────────────────────── */

static void _ns_write_value(Value* v) {
    if (!v || !v->obj || !v->obj->ob_type) { _ns_put_str("None"); return; }
    NvTypeObject* t = v->obj->ob_type;
    if (t == NVStr_Type)        _ns_put_str(((NVStr*)v->obj)->value);
    else if (t == NVInt_Type)   _ns_put_int((long)((NVInt*)v->obj)->value);
    else if (t == NVFloat_Type) _ns_put_float(((NVFloat*)v->obj)->value);
    else if (t == NVBool_Type)  _ns_put_str(((NVBool*)v->obj)->value ? "true" : "false");
    else if (t == NVChar_Type)  _ns_write_bytes(&((NVChar*)v->obj)->value, 1);
    else if (t == NVVector_Type || t == NVArray_Type) {
        NVVector* vec = (NVVector*)v->obj;
        _ns_put_str("[");
        for (int i = 0; i < vec->size; ++i) {
            if (i) _ns_put_str(", ");
            Value e = vec->elements[i];
            _ns_write_value(&e);
        }
        _ns_put_str("]");
    }
    else                        _ns_put_str("<object>");
}

void nv_write(Value* v) {
    _ns_write_value(v);
    _ns_put_str("\n");
}

void nv_write_no_nl(Value* v) {
    _ns_write_value(v);
}

/* The codegen emits nv_write_bridge for a `write(x)` statement. */
NvObject* nv_write_bridge(NvObject* obj) {
    Value v = {obj};
    nv_write(&v);
    return (NvObject*)0;
}

/* ── Collections ─────────────────────────────────────────────────────────────
 * Same element layout as the std runtime (Value* / size / capacity) so the
 * functions that read them — nv_write, nv_container_get — agree. The allocator is
 * the bump arena: growth copies into a bigger block and leaks the old one.
 */

NvObject* nv_make_none(void) {
    _ns_ensure_types();
    NvObject* o = (NvObject*)_nv_ns_alloc(sizeof(NvObject));
    if (o) o->ob_type = NVOptionNone_Type;
    return o;
}

static Value* _ns_alloc_elems(int cap) {
    return (Value*)_nv_ns_alloc((size_t)cap * sizeof(Value));
}

NvObject* nv_create_vector(NvObject* sz_obj) {
    _ns_ensure_types();
    int n = 0;
    if (sz_obj && sz_obj->ob_type == NVInt_Type) n = ((NVInt*)sz_obj)->value;
    if (n < 0) n = 0;
    if (n == 0) n = 4;
    NVVector* v = (NVVector*)_nv_ns_alloc(sizeof(NVVector));
    if (!v) return (NvObject*)0;
    v->ob_base.ob_type = NVVector_Type;
    v->elements  = _ns_alloc_elems(n);
    v->size      = 0;
    v->capacity  = n;
    return (NvObject*)v;
}

/* The codegen may ask for an array; in this runtime it is the same object. */
NvObject* nv_create_array(NvObject* sz_obj) {
    NvObject* v = nv_create_vector(sz_obj);
    if (v) v->ob_type = NVArray_Type;   /* v is NvObject* here */
    return v;
}

void nv_vector_push(NvObject* vec_obj, NvObject* elem_obj) {
    if (!vec_obj || (vec_obj->ob_type != NVVector_Type && vec_obj->ob_type != NVArray_Type))
        return;
    NVVector* v = (NVVector*)vec_obj;
    if (v->size >= v->capacity) {
        int cap = v->capacity ? v->capacity * 2 : 4;
        Value* ne = _ns_alloc_elems(cap);
        if (!ne) return;
        for (int i = 0; i < v->size; ++i) ne[i] = v->elements[i];
        v->elements = ne;
        v->capacity = cap;
    }
    v->elements[v->size].obj = elem_obj;
    v->size += 1;
}

static int _ns_index_of(NvObject* key_obj, int size) {
    int i = ((NVInt*)key_obj)->value;
    if (i < 0) i += size;
    return i;
}

NvObject* nv_container_get(NvObject* base_obj, NvObject* key_obj) {
    if (!base_obj || !key_obj) return nv_make_none();
    _ns_ensure_types();
    if (base_obj->ob_type == NVVector_Type || base_obj->ob_type == NVArray_Type) {
        if (key_obj->ob_type != NVInt_Type) return nv_make_none();
        NVVector* v = (NVVector*)base_obj;
        const int i = _ns_index_of(key_obj, v->size);
        if (i < 0 || i >= v->size) return nv_make_none();
        NvObject* e = v->elements[i].obj;
        return e ? e : nv_make_none();
    }
    if (base_obj->ob_type == NVStr_Type) {
        /* s[i] is a one-character string, as in the std runtime. */
        const char* str = ((NVStr*)base_obj)->value;
        if (!str || key_obj->ob_type != NVInt_Type) return nv_make_none();
        const int i = ((NVInt*)key_obj)->value;
        if (i < 0 || !str[i]) return nv_make_none();
        char buf[2] = { str[i], 0 };
        Value out = {0};
        create_str(&out, buf);
        return out.obj;
    }
    return nv_make_none();
}

void nv_container_set(NvObject* base_obj, NvObject* key_obj, NvObject* value_obj) {
    if (!base_obj || !key_obj) return;
    _ns_ensure_types();
    if (base_obj->ob_type != NVVector_Type && base_obj->ob_type != NVArray_Type) return;
    if (key_obj->ob_type != NVInt_Type) return;
    NVVector* v = (NVVector*)base_obj;
    const int i = _ns_index_of(key_obj, v->size);
    if (i < 0 || i >= v->size) return;
    v->elements[i].obj = value_obj;
}

int32_t nv_container_len(NvObject* base_obj) {
    if (!base_obj) return 0;
    if (base_obj->ob_type == NVVector_Type || base_obj->ob_type == NVArray_Type)
        return ((NVVector*)base_obj)->size;
    if (base_obj->ob_type == NVStr_Type) {
        const char* str = ((NVStr*)base_obj)->value;
        return str ? (int32_t)_ns_strlen(str) : 0;
    }
    return 0;
}

/* ── Conversions (int / float / str / bool / char) ────────────────────────────
 * Same results as the std runtime: str(float) is printf("%f"), str(7) is "7".
 */

static int _ns_fmt_int(char* buf, long v) {
    int i = 0;
    int neg = v < 0;
    unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1UL : (unsigned long)v;
    char tmp[24];
    int n = 0;
    if (u == 0) tmp[n++] = '0';
    while (u) { tmp[n++] = (char)('0' + (u % 10)); u /= 10; }
    if (neg) buf[i++] = '-';
    while (n) buf[i++] = tmp[--n];
    buf[i] = 0;
    return i;
}

static int _ns_fmt_float(char* buf, double d) {
    int i = 0;
    if (d != d) { buf[0] = 'n'; buf[1] = 'a'; buf[2] = 'n'; buf[3] = 0; return 3; }
    if (d < 0) { buf[i++] = '-'; d = -d; }
    unsigned long ip = (unsigned long)d;
    double frac = d - (double)ip;
    unsigned long fp = (unsigned long)(frac * 1000000.0 + 0.5);
    if (fp >= 1000000UL) { ip += 1; fp -= 1000000UL; }
    i += _ns_fmt_int(buf + i, (long)ip);
    buf[i++] = '.';
    char dec[6];
    for (int k = 5; k >= 0; --k) { dec[k] = (char)('0' + (fp % 10)); fp /= 10; }
    for (int k = 0; k < 6; ++k) buf[i++] = dec[k];
    buf[i] = 0;
    return i;
}

static long _ns_parse_int(const char* s) {
    if (!s) return 0;
    long sign = 1, v = 0;
    if (*s == '-') { sign = -1; ++s; } else if (*s == '+') ++s;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return sign * v;
}

static double _ns_parse_float(const char* s) {
    if (!s) return 0.0;
    double sign = 1.0, v = 0.0;
    if (*s == '-') { sign = -1.0; ++s; } else if (*s == '+') ++s;
    while (*s >= '0' && *s <= '9') { v = v * 10.0 + (double)(*s - '0'); ++s; }
    if (*s == '.') {
        ++s;
        double scale = 0.1;
        while (*s >= '0' && *s <= '9') { v += (double)(*s - '0') * scale; scale *= 0.1; ++s; }
    }
    return sign * v;
}

NvObject* nv_int_builtin(NvObject* o) {
    _ns_ensure_types();
    long v = 0;
    if (o) {
        if      (o->ob_type == NVInt_Type)   v = ((NVInt*)o)->value;
        else if (o->ob_type == NVFloat_Type) v = (long)((NVFloat*)o)->value;
        else if (o->ob_type == NVBool_Type)  v = ((NVBool*)o)->value;
        else if (o->ob_type == NVChar_Type)  v = ((NVChar*)o)->value;
        else if (o->ob_type == NVStr_Type)   v = _ns_parse_int(((NVStr*)o)->value);
    }
    Value out = {0};
    create_int(&out, (int32_t)v);
    return out.obj;
}

NvObject* nv_float_builtin(NvObject* o) {
    _ns_ensure_types();
    double v = 0.0;
    if (o) {
        if      (o->ob_type == NVFloat_Type) v = ((NVFloat*)o)->value;
        else if (o->ob_type == NVInt_Type)   v = (double)((NVInt*)o)->value;
        else if (o->ob_type == NVBool_Type)  v = (double)((NVBool*)o)->value;
        else if (o->ob_type == NVChar_Type)  v = (double)((NVChar*)o)->value;
        else if (o->ob_type == NVStr_Type)   v = _ns_parse_float(((NVStr*)o)->value);
    }
    Value out = {0};
    create_float(&out, v);
    return out.obj;
}

NvObject* nv_str_builtin(NvObject* o) {
    _ns_ensure_types();
    char buf[64];
    Value out = {0};
    if (!o || !o->ob_type) { create_str(&out, ""); return out.obj; }
    if (o->ob_type == NVStr_Type)  { create_str(&out, ((NVStr*)o)->value ? ((NVStr*)o)->value : ""); return out.obj; }
    if (o->ob_type == NVInt_Type)  { _ns_fmt_int(buf, ((NVInt*)o)->value); create_str(&out, buf); return out.obj; }
    if (o->ob_type == NVFloat_Type){ _ns_fmt_float(buf, ((NVFloat*)o)->value); create_str(&out, buf); return out.obj; }
    if (o->ob_type == NVBool_Type) { create_str(&out, ((NVBool*)o)->value ? "true" : "false"); return out.obj; }
    if (o->ob_type == NVChar_Type) { char c[2] = { ((NVChar*)o)->value, 0 }; create_str(&out, c); return out.obj; }
    create_str(&out, "<object>");
    return out.obj;
}

NvObject* nv_bool_builtin(NvObject* o) {
    _ns_ensure_types();
    Value out = {0};
    create_bool(&out, nv_value_is_truthy(o) ? 1 : 0);
    return out.obj;
}

NvObject* nv_char_builtin(NvObject* o) {
    _ns_ensure_types();
    char c = 0;
    if (o) {
        if      (o->ob_type == NVChar_Type)  c = ((NVChar*)o)->value;
        else if (o->ob_type == NVInt_Type)   c = (char)((NVInt*)o)->value;
        else if (o->ob_type == NVStr_Type && ((NVStr*)o)->value) c = ((NVStr*)o)->value[0];
    }
    Value out = {0};
    create_char(&out, c);
    return out.obj;
}

/* The std runtime runs global initialisers here; there are none in no_std. */
void register_global_init(void) { }
