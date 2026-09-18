// nir_bridge.c — Value-returning bridge functions for the NIR (MLIR) pipeline.
//
// In the NIR pipeline, !narval.value lowers to !llvm.ptr, and values are passed
// as NvObject* (the inner pointer from Value.obj).  These bridges wrap the
// existing output-parameter runtime functions and expose the value-returning ABI
// that the NIR-generated LLVM IR expects.

#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/win32_compat.h"   // dlopen/dlsym and the cpu count on Windows
#include <pthread.h>
#ifndef _WIN32
#include <unistd.h>   // sysconf: how many cpus the parallel loop may use
#endif
#include <ctype.h>
#include <math.h>     // floor/pow: `//` and `**`
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif

// Internal helpers declared in value.c but not in headers.
void tuple_set_impl(Value* self, int32_t index, Value* elem);
int  binary_add(Value* result, Value* lhs, Value* rhs);

// ── Helpers ─────────────────────────────────────────────────────────────────

static int32_t obj_to_i32(NvObject* obj) {
    if (!obj) return 0;
    if (obj->ob_type == NVInt_Type)   return ((NVInt*)obj)->value;
    if (obj->ob_type == NVFloat_Type) return (int32_t)((NVFloat*)obj)->value;
    if (obj->ob_type == NVBool_Type)  return ((NVBool*)obj)->value;
    return 0;
}

static int32_t obj_to_i32_or_sentinel(NvObject* obj) {
    if (!obj) return (int32_t)(-2147483648);
    if (obj->ob_type == NVInt_Type) return ((NVInt*)obj)->value;
    return (int32_t)(-2147483648);
}

static int obj_is_truthy(NvObject* obj) {
    if (!obj) return 0;
    if (obj->ob_type == NVBool_Type)  return ((NVBool*)obj)->value  != 0;
    if (obj->ob_type == NVInt_Type)   return ((NVInt*)obj)->value   != 0;
    if (obj->ob_type == NVFloat_Type) return ((NVFloat*)obj)->value != 0.0;
    if (obj->ob_type == NVStr_Type) {
        const char* s = ((NVStr*)obj)->value;
        return s && s[0] != '\0';
    }
    if (obj->ob_type == NVOptionNone_Type) return 0;
    return 1;
}

// ── Box primitives ───────────────────────────────────────────────────────────

NvObject* nv_box_int(int64_t v) {
    Value out = {NULL};
    create_int(&out, v);
    return out.obj;
}

// Raw integer behind a boxed value, the counterpart of nv_box_int. Inline assembly (and
// anything else that hands a value to a raw instruction) needs an i64 operand, and the
// tensor helper nv_value_to_i64 is not usable here: it takes the internal `Value*` wrapper,
// not the `NvObject*` the NIR ABI passes, so it read past the box and returned 0.
int64_t nv_unbox_int(NvObject* obj) {
    if (!obj) return 0;
    if (obj->ob_type == NVInt_Type)   return (int64_t)((NVInt*)obj)->value;
    if (obj->ob_type == NVFloat_Type) return (int64_t)((NVFloat*)obj)->value;
    if (obj->ob_type == NVBool_Type)  return ((NVBool*)obj)->value ? 1 : 0;
    return 0;
}

NvObject* nv_box_float(double v) {
    Value out = {NULL};
    create_float(&out, v);
    return out.obj;
}

// Booleans are boxed as NV_BOOL values. A raw int constant made `write(true)`
// print -1 and `write(false)` print 0, and lost the type in mixed expressions.
NvObject* nv_box_bool(int64_t v) {
    Value out = {NULL};
    create_bool(&out, v != 0 ? 1 : 0);
    return out.obj;
}

NvObject* nv_box_str(const char* s) {
    Value out = {NULL};
    create_str(&out, s ? s : "");
    return out.obj;
}

// ── Arithmetic ───────────────────────────────────────────────────────────────

NvObject* nv_add(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    if (!binary_add(&out, &va, &vb))
        nv_value_add(&out, &va, &vb);
    return out.obj;
}

NvObject* nv_sub(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    nv_value_sub(&out, &va, &vb);
    return out.obj;
}

NvObject* nv_mul(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    nv_value_mul(&out, &va, &vb);
    return out.obj;
}

NvObject* nv_div(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    nv_value_div(&out, &va, &vb);
    return out.obj;
}

NvObject* nv_mod(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    nv_value_mod(&out, &va, &vb);
    return out.obj;
}

// ── Integer division, power and bitwise operators ────────────────────────────
//
// These had no case in the codegen's operator table, so `//`, `**`, `&`, `|`,
// `^`, `<<` and `>>` all fell through to the `+` default: `7 // 2` printed 9 and
// `2 ** 10` printed 12.

// A boxed integer-ish value as a C int64 (a float operand truncates, like C).
static int64_t nv_obj_to_i64(NvObject* o) {
    if (!o) return 0;
    NvTypeObject* t = o->ob_type;
    if (t == NVInt_Type)   return ((NVInt*)o)->value;
    if (t == NVFloat_Type) return (int64_t)((NVFloat*)o)->value;
    if (t == NVBool_Type)  return (int64_t)((NVBool*)o)->value;
    if (t == NVChar_Type)  return (int64_t)((NVChar*)o)->value;
    return 0;
}

// Defined further down, next to the tensor element helpers.
static double nv_obj_to_f64(NvObject* o);

static int nv_obj_is_float(NvObject* o) {
    return o && o->ob_type == NVFloat_Type;
}

// `//`: the integer quotient. Truncating, to agree with `%` (a == b*(a//b) + a%b
// holds for both signs). With a float operand it is the floored quotient, which
// is what a float division without the fraction means.
NvObject* nv_floor_div(NvObject* a, NvObject* b) {
    Value out = {NULL};
    if (nv_obj_is_float(a) || nv_obj_is_float(b)) {
        double vb = nv_obj_to_f64(b);
        if (vb == 0.0) return NULL;
        create_float(&out, floor(nv_obj_to_f64(a) / vb));
        return out.obj;
    }
    int64_t vb = nv_obj_to_i64(b);
    if (vb == 0) return NULL;
    create_int(&out, nv_obj_to_i64(a) / vb);
    return out.obj;
}

// `**`: integer base and exponent stay integers (2 ** 10 is 1024, exact while it
// fits); a negative exponent or a float operand goes through pow().
NvObject* nv_pow(NvObject* a, NvObject* b) {
    Value out = {NULL};
    if (!nv_obj_is_float(a) && !nv_obj_is_float(b)) {
        int64_t base = nv_obj_to_i64(a);
        int64_t exp  = nv_obj_to_i64(b);
        if (exp >= 0) {
            int64_t result = 1;
            int64_t factor = base;
            while (exp > 0) {
                if (exp & 1) result *= factor;
                exp >>= 1;
                if (exp) factor *= factor;
            }
            create_int(&out, result);
            return out.obj;
        }
    }
    create_float(&out, pow(nv_obj_to_f64(a), nv_obj_to_f64(b)));
    return out.obj;
}

NvObject* nv_band(NvObject* a, NvObject* b) {
    Value out = {NULL};
    create_int(&out, nv_obj_to_i64(a) & nv_obj_to_i64(b));
    return out.obj;
}

NvObject* nv_bor(NvObject* a, NvObject* b) {
    Value out = {NULL};
    create_int(&out, nv_obj_to_i64(a) | nv_obj_to_i64(b));
    return out.obj;
}

NvObject* nv_bxor(NvObject* a, NvObject* b) {
    Value out = {NULL};
    create_int(&out, nv_obj_to_i64(a) ^ nv_obj_to_i64(b));
    return out.obj;
}

// Shifts are masked to the width of the left operand, so `1 << 64` is `1 << 0`
// instead of undefined behavior.
NvObject* nv_shl(NvObject* a, NvObject* b) {
    Value out = {NULL};
    create_int(&out, (int64_t)((uint64_t)nv_obj_to_i64(a) << (nv_obj_to_i64(b) & 63)));
    return out.obj;
}

NvObject* nv_shr(NvObject* a, NvObject* b) {
    Value out = {NULL};
    create_int(&out, nv_obj_to_i64(a) >> (nv_obj_to_i64(b) & 63));
    return out.obj;
}

// ── Comparison ───────────────────────────────────────────────────────────────

NvObject* nv_value_eq(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    create_bool(&out, nv_value_cmp(&va, &vb) == 0);
    return out.obj;
}

int nv_value_eq_bool(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b};
    return nv_value_cmp(&va, &vb) == 0 ? 1 : 0;
}

NvObject* nv_value_ne(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    create_bool(&out, nv_value_cmp(&va, &vb) != 0);
    return out.obj;
}

NvObject* nv_value_lt(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    create_bool(&out, nv_value_cmp(&va, &vb) < 0);
    return out.obj;
}

NvObject* nv_value_le(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    create_bool(&out, nv_value_cmp(&va, &vb) <= 0);
    return out.obj;
}

NvObject* nv_value_gt(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    create_bool(&out, nv_value_cmp(&va, &vb) > 0);
    return out.obj;
}

NvObject* nv_value_ge(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    create_bool(&out, nv_value_cmp(&va, &vb) >= 0);
    return out.obj;
}

// ── Logical ──────────────────────────────────────────────────────────────────

int nv_value_is_truthy(NvObject* obj) {
    return obj_is_truthy(obj);
}

NvObject* nv_value_and(NvObject* a, NvObject* b) {
    Value out = {NULL};
    create_bool(&out, obj_is_truthy(a) && obj_is_truthy(b));
    return out.obj;
}

NvObject* nv_value_or(NvObject* a, NvObject* b) {
    Value out = {NULL};
    create_bool(&out, obj_is_truthy(a) || obj_is_truthy(b));
    return out.obj;
}

NvObject* nv_value_not(NvObject* a) {
    Value out = {NULL};
    create_bool(&out, !obj_is_truthy(a));
    return out.obj;
}

// ── Field access ─────────────────────────────────────────────────────────────

NvObject* nv_get_field(NvObject* obj, const char* key) {
    if (!obj || !key) return NULL;
    Value self = {obj}, result = {NULL};
    nv_object_get_field(&result, &self, key);
    nv_incref(result.obj);   // a field read owns its reference too
    return result.obj;
}

void nv_set_field(NvObject* obj, const char* key, NvObject* val_obj) {
    if (!obj || !key) return;
    Value self = {obj}, val = {val_obj};
    nv_object_set_field(&self, key, &val);
}

// ── Collections ───────────────────────────────────────────────────────────────

NvObject* nv_create_map(void) {
    Value out = {NULL};
    create_map(&out);
    return out.obj;
}

NvObject* nv_create_array(NvObject* sz_obj) {
    int32_t n = sz_obj ? obj_to_i32(sz_obj) : 0;
    if (n < 0) n = 0;
    Value out = {NULL};
    create_array(&out, n);
    return out.obj;
}

void nv_array_set(NvObject* arr_obj, NvObject* idx_obj, NvObject* elem_obj) {
    if (!arr_obj) return;
    int32_t idx = obj_to_i32(idx_obj);
    Value arr = {arr_obj}, elem = {elem_obj};
    array_set_index_v(&arr, idx, &elem);
}

NvObject* nv_array_get(NvObject* arr_obj, NvObject* idx_obj) {
    if (!arr_obj) return NULL;
    int32_t idx = obj_to_i32(idx_obj);
    Value arr = {arr_obj}, out = {NULL};
    array_get_index_v(&out, &arr, idx);
    // Reading PROMOTES the element to an owner (the "can_promote_to_owned" branch of
    // OWNERSHIP_DESIGN Fase 3): the caller gets a reference of its own, so the value it
    // read may outlive the container. Without the incref this is a pointer INTO the
    // container and whether it survives is decided by drop ordering, not by semantics —
    // which is what the last_consumer dance in the drops pass was papering over.
    nv_incref(out.obj);
    return out.obj;
}

// Generic container access dispatched on the receiver type: maps are keyed by
// string (like field access), arrays/vectors/tuples by integer index.
// Used by AccessExprNode (m["k"] / v[i]) which cannot know the static type.
// Unbox a number as a double, for the tensor element paths (which store f64).
static double nv_obj_to_f64(NvObject* obj) {
    if (!obj) return 0.0;
    if (obj->ob_type == NVFloat_Type) return ((NVFloat*)obj)->value;
    if (obj->ob_type == NVInt_Type)   return (double)((NVInt*)obj)->value;
    if (obj->ob_type == NVBool_Type)  return (double)((NVBool*)obj)->value;
    if (obj->ob_type == NVChar_Type)  return (double)((NVChar*)obj)->value;
    return 0.0;
}

// Bridge: nv_tensor_flat_index(tensor, n, i0, i1, i2, i3) -> boxed flat index
// The row-major offset of an element addressed by n coordinates, taken from the tensor's
// own shape — so multi-index access needs no static shape and works with dynamic
// dimensions. Coordinates past the rank are ignored, which lets the codegen pass a
// fixed-width argument list for any rank.
NvObject* nv_tensor_flat_index(NvObject* t, NvObject* n_obj,
                               NvObject* i0, NvObject* i1,
                               NvObject* i2, NvObject* i3) {
    if (!t) return nv_box_int(0);
    NvObject* coords[4] = {i0, i1, i2, i3};
    Value     tv        = {t};
    int32_t   ndim      = nv_tensor_ndim(&tv);
    int32_t   n         = obj_to_i32(n_obj);
    int64_t   flat      = 0;
    for (int32_t k = 0; k < n && k < 4 && k < ndim; ++k) {
        int64_t stride = 1;
        for (int32_t d = k + 1; d < ndim; ++d) stride *= nv_tensor_dim(&tv, d);
        flat += (int64_t)obj_to_i32(coords[k]) * stride;
    }
    return nv_box_int(flat);
}

// A map key as text. Map keys are C strings, so a scalar key has to be rendered:
// without this, `m = {1: "one"}; m[1]` found nothing (the lookup accepted strings
// only) while `m[1] = v` silently stored nothing.
static const char* nv_map_key_text(NvObject* key_obj, char* buf, size_t buf_size) {
    if (!key_obj) return NULL;
    NvTypeObject* t = key_obj->ob_type;
    if (t == NVStr_Type)   return ((NVStr*)key_obj)->value;
    if (t == NVInt_Type)   { snprintf(buf, buf_size, "%lld", (long long)((NVInt*)key_obj)->value); return buf; }
    if (t == NVBool_Type)  { snprintf(buf, buf_size, "%s", ((NVBool*)key_obj)->value ? "true" : "false"); return buf; }
    if (t == NVChar_Type)  { snprintf(buf, buf_size, "%c", ((NVChar*)key_obj)->value); return buf; }
    if (t == NVFloat_Type) { snprintf(buf, buf_size, "%g", ((NVFloat*)key_obj)->value); return buf; }
    return NULL;
}

NvObject* nv_container_get(NvObject* base_obj, NvObject* key_obj) {
    if (!base_obj || !key_obj) return NULL;
    // A tensor: flat element access over its contiguous buffer. nv_tensor_data_ptr is NULL
    // for anything that is not a float tensor, which is exactly the guard needed here.
    {
        Value tv = {base_obj};
        if (nv_tensor_data_ptr(&tv)) {          // NULL for anything that is not a tensor
            return nv_tensor_get_element(&tv, obj_to_i32(key_obj));
        }
    }
    if (base_obj->ob_type == NVMap_Type) {
        // A map is a table of names, but a key may be any scalar: 1 and "1" name
        // the same entry. Refusing a non-string key made `m = {1: "one"}; m[1]`
        // read nothing at all.
        char key_buf[64];
        const char* key = nv_map_key_text(key_obj, key_buf, sizeof(key_buf));
        if (!key) return NULL;
        Value self = {base_obj}, out = {NULL};
        nv_object_get_field(&out, &self, key);
        nv_incref(out.obj);   // promoted to an owner, like nv_array_get above
        return out.obj;
    }
    if (base_obj->ob_type == NVStr_Type) {
        // s[i] is a one-character string (check_access_expr: a string indexed by
        // an int yields a string). Falling through to nv_array_get read the
        // string object as a vector/array and segfaulted. Out-of-range indices
        // give "" rather than reading past the buffer.
        const char* s = ((NVStr*)base_obj)->value;
        int32_t i = obj_to_i32(key_obj);
        Value out = {NULL};
        if (!s || i < 0 || (size_t)i >= strlen(s)) {
            create_str(&out, "");
            return out.obj;
        }
        char buf[2] = { s[i], '\0' };
        create_str(&out, buf);
        return out.obj;
    }
    return nv_array_get(base_obj, key_obj);
}

// Write side of nv_container_get, for the same reason: `a[i] = v` reaches here
// without a static type. Calling nv_array_set on a map read the STRING key as an
// integer index, so the assignment landed on whatever index the pointer happened to
// be — the entry it meant to replace came back empty on the next read. A map is a
// field table, so its store is nv_object_set_field, exactly like the get above.
void nv_container_set(NvObject* base_obj, NvObject* key_obj, NvObject* val_obj) {
    if (!base_obj || !key_obj) return;
    // The tensor element is the one place a value is written as a plain f64.
    {
        Value tv = {base_obj};
        if (nv_tensor_data_ptr(&tv)) {
            nv_tensor_set_element(&tv, obj_to_i32(key_obj), val_obj);
            return;
        }
    }
    if (base_obj->ob_type == NVMap_Type) {
        char key_buf[64];
        const char* key = nv_map_key_text(key_obj, key_buf, sizeof(key_buf));
        if (!key) return;
        Value self = {base_obj}, val = {val_obj};
        nv_object_set_field(&self, key, &val);
        return;
    }
    if (base_obj->ob_type == NVStr_Type) return;  // strings are immutable
    Value self = {base_obj}, val = {val_obj};
    array_set_index_v(&self, obj_to_i32(key_obj), &val);
}

NvObject* nv_create_vector(NvObject* sz_obj) {
    int32_t n = sz_obj ? obj_to_i32(sz_obj) : 4;
    if (n < 0) n = 4;
    Value out = {NULL};
    create_vector(&out, n);
    return out.obj;
}

void nv_vector_push(NvObject* vec_obj, NvObject* elem_obj) {
    if (!vec_obj) return;
    Value vec = {vec_obj}, elem = {elem_obj};
    vector_push_method(NULL, &vec, &elem);
}

NvObject* nv_create_tuple(NvObject* sz_obj) {
    int32_t n = sz_obj ? obj_to_i32(sz_obj) : 0;
    if (n < 0) n = 0;
    Value out = {NULL};
    create_tuple(&out, n);
    return out.obj;
}

void nv_tuple_set(NvObject* tup_obj, NvObject* idx_obj, NvObject* elem_obj) {
    if (!tup_obj) return;
    int32_t idx = obj_to_i32(idx_obj);
    Value tup = {tup_obj}, elem = {elem_obj};
    tuple_set_impl(&tup, idx, &elem);
}

void nv_map_set_dynamic(NvObject* map_obj, NvObject* key_obj, NvObject* val_obj) {
    if (!map_obj || !key_obj) return;
    char key_buf[64];
    const char* key = nv_map_key_text(key_obj, key_buf, sizeof(key_buf));
    if (!key) return;
    Value map = {map_obj}, val = {val_obj};
    nv_object_set_field(&map, key, &val);
}

// A range value is the map nv_make_range builds: __type__ == "range" with
// start/end/inclusive. Iterating it must walk the numbers — with the map's own
// length and key-at-index behaviour, `[x for x in 0..5]` produced the four field
// names instead of 0..4.
static int nv_range_of(NvObject* obj, int64_t* first, int64_t* count) {
    if (!obj || obj->ob_type != NVMap_Type) return 0;
    Value self = {obj};
    Value tag = {NULL};
    nv_object_get_field(&tag, &self, "__type__");
    if (!tag.obj || tag.obj->ob_type != NVStr_Type) return 0;
    const char* name = ((NVStr*)tag.obj)->value;
    if (!name || strcmp(name, "range") != 0) return 0;

    Value s = {NULL}, e = {NULL}, inc = {NULL};
    nv_object_get_field(&s, &self, "start");
    nv_object_get_field(&e, &self, "end");
    nv_object_get_field(&inc, &self, "inclusive");
    int64_t from = nv_obj_to_i64(s.obj);
    int64_t to   = nv_obj_to_i64(e.obj);
    if (inc.obj && nv_obj_to_i64(inc.obj)) to += 1;
    *first = from;
    *count = to > from ? to - from : 0;
    return 1;
}

// ── Iteration ─────────────────────────────────────────────────────────────────

int32_t nv_len(NvObject* obj) {
    if (!obj) return 0;
    int64_t first = 0, count = 0;
    if (nv_range_of(obj, &first, &count)) return (int32_t)count;
    Value v = {obj};
    return nv_get_iterable_length(&v);
}

// Boxed length for the language-level `len(x)` builtin (see builtins.cpp).
// nv_len above returns a raw int32 for the for-in lowering, which cannot be
// used as a value; this wrapper returns a normal boxed int.
NvObject* nv_len_builtin(NvObject* obj) {
    Value out = {NULL};
    create_int(&out, nv_len(obj));
    return out.obj;
}

// Generic index read for the for-in lowering and dynamic indexing: the receiver
// decides what an index means. A string yields a one-character string, a map
// yields its i-th key (so `for k in m` walks the keys), a tuple its i-th field.
// Strings and maps calling into array_get_index_v read the object as a vector:
// `for c in "abc"` segfaulted and `for k in m` ran zero times.
NvObject* nv_get_at(NvObject* arr_obj, int32_t idx) {
    if (!arr_obj) return NULL;
    int64_t first = 0, count = 0;
    if (nv_range_of(arr_obj, &first, &count)) {
        if (idx < 0 || (int64_t)idx >= count) return nv_box_int(0);
        return nv_box_int(first + idx);
    }
    if (arr_obj->ob_type == NVStr_Type) {
        const char* s = ((NVStr*)arr_obj)->value;
        Value out = {NULL};
        if (!s || idx < 0 || (size_t)idx >= strlen(s)) { create_str(&out, ""); return out.obj; }
        char buf[2] = { s[idx], '\0' };
        create_str(&out, buf);
        return out.obj;
    }
    if (arr_obj->ob_type == NVMap_Type) {
        NVMap* m = (NVMap*)arr_obj;
        Value out = {NULL};
        if (idx < 0 || idx >= m->size) { create_str(&out, ""); return out.obj; }
        create_str(&out, m->keys[idx] ? m->keys[idx] : "");
        return out.obj;
    }
    if (arr_obj->ob_type == NVTuple_Type) {
        NVTuple* t = (NVTuple*)arr_obj;
        Value out = {NULL};
        if (idx < 0 || idx >= t->field_count) return NULL;
        out = t->fields[idx];
        return out.obj;
    }
    Value arr = {arr_obj}, out = {NULL};
    array_get_index_v(&out, &arr, idx);
    return out.obj;
}

size_t nv_value_to_index(NvObject* obj) {
    return (size_t)(size_t)(obj ? (size_t)(uint32_t)obj_to_i32(obj) : 0);
}

NvObject* nv_index_to_value(size_t idx) {
    Value out = {NULL};
    create_int(&out, (int32_t)idx);
    return out.obj;
}

// Select between two values based on a boolean condition.
// Used by ternary expressions to avoid narval.if with result types.
// Returns an OWNED reference to the selected value. The caller passes the
// branches as temporaries and drops them right after the call, and the winner is
// one of those temporaries, so the extra reference is what keeps the result
// alive (without it the value was freed and came back as a corrupt object).
NvObject* nv_select(int cond, NvObject* a, NvObject* b) {
    NvObject* chosen = cond ? a : b;
    nv_incref(chosen);
    return chosen;
}

// ── Range ─────────────────────────────────────────────────────────────────────

NvObject* nv_make_range(NvObject* start_obj, NvObject* end_obj, NvObject* inc_obj) {
    Value range = {NULL};
    create_map(&range);
    if (!range.obj) return NULL;

    Value vt = {NULL}; create_str(&vt, "range");
    Value vs = {start_obj};
    Value ve = {end_obj};
    Value vi = {NULL};
    if (inc_obj) vi.obj = inc_obj; else create_bool(&vi, 0);

    // The map owns what it stores: without the references the ownership pass freed
    // the boxed bounds right after the nv_make_range call (their last use), and
    // every later read of the range saw zeroed fields — `[x for x in 1..4]` came
    // back as three zeros.
    nv_incref(vt.obj);
    nv_incref(vs.obj);
    nv_incref(ve.obj);
    nv_incref(vi.obj);
    nv_object_set_field(&range, "__type__",  &vt);
    nv_object_set_field(&range, "start",     &vs);
    nv_object_set_field(&range, "end",       &ve);
    nv_object_set_field(&range, "inclusive", &vi);
    return range.obj;
}

// ── Slice ─────────────────────────────────────────────────────────────────────

NvObject* nv_slice(NvObject* base_obj, NvObject* s_obj, NvObject* e_obj,
                   NvObject* step_obj) {
    if (!base_obj) return NULL;
    Value base = {base_obj}, out = {NULL};
    int32_t s    = obj_to_i32_or_sentinel(s_obj);
    int32_t e    = obj_to_i32_or_sentinel(e_obj);
    int32_t step = obj_to_i32_or_sentinel(step_obj);
    nv_collection_slice(&out, &base, s, e, step);
    return out.obj;
}

// ── None / Option / Result ────────────────────────────────────────────────────

// ── Result wrapping ──────────────────────────────────────────────────────────
// Used by narval.result_wrap lowering and LowerNarvalErrorHandlingPass.

NvObject* nv_make_ok(NvObject* val) {
    Value inner = {val}, out = {NULL};
    create_result_ok(&out, &inner);
    return out.obj;
}

NvObject* nv_make_err(NvObject* val) {
    Value inner = {val}, out = {NULL};
    create_result_err(&out, &inner);
    return out.obj;
}

// Unwrap Ok/Some inner value. Returns NULL if the value is not Ok/Some.
NvObject* nv_unwrap_result(NvObject* obj) {
    if (!obj) return NULL;
    Value out = {NULL};
    Value v = {obj};
    nv_unwrap_inner(&out, &v);
    return out.obj;
}

// ── Class instantiation ──────────────────────────────────────────────────────
// Used by narval.new lowering (LowerNarvalClassesPass).
// Creates an empty NVMap-backed object and stamps __class_name__ = class_name.

NvObject* nv_alloc_object(const char* class_name) {
    Value out = {NULL};
    create_map(&out);
    if (!out.obj) return NULL;
    Value cn_val = {NULL};
    create_str(&cn_val, class_name);
    nv_object_set_field(&out, "__class_name__", &cn_val);
    return out.obj;
}

// ── None / Option / Result ────────────────────────────────────────────────────

NvObject* nv_make_none(void) {
    Value out = {NULL};
    create_option_none(&out);
    return out.obj;
}

// Returns a plain int 0/1, like nv_value_is_truthy: the codegen reads the result
// as i1, and a boxed bool is a POINTER — the low bit of an aligned address
// decided the branch, so `Some(7) or 42` always printed 42 (the fallback).
int nv_value_is_some_or_ok(NvObject* obj) {
    return (obj && (obj->ob_type == NVOptionSome_Type ||
                    obj->ob_type == NVResultOk_Type)) ? 1 : 0;
}

// The error an `or` handler sees: the payload of an Err, None otherwise. This is
// what `err` is bound to inside `x = Err("bad") or { err }` / `... or err`.
NvObject* nv_or_error(NvObject* base) {
    Value out = {NULL};
    if (base && base->ob_type == NVResultErr_Type) {
        out = ((NVResultErr*)base)->inner;
        nv_incref(out.obj);   // the handler owns its own reference
        return out.obj;
    }
    return nv_make_none();
}

// ── Instanceof ────────────────────────────────────────────────────────────────

NvObject* nv_instanceof(NvObject* obj, NvObject* class_name_obj) {
    Value out = {NULL};
    if (!obj || !class_name_obj) { create_bool(&out, 0); return out.obj; }

    const char* want = NULL;
    if (class_name_obj->ob_type == NVStr_Type)
        want = ((NVStr*)class_name_obj)->value;
    if (!want) { create_bool(&out, 0); return out.obj; }

    int match = 0;
    const char* tp = obj->ob_type ? obj->ob_type->tp_name : NULL;
    if (tp && strcmp(tp, want) == 0) match = 1;

    // Also check __class_name__ field for map-backed class instances.
    if (!match && obj->ob_type == NVMap_Type) {
        NVMap* m = (NVMap*)obj;
        for (int i = 0; i < m->size; i++) {
            if (m->keys[i] && strcmp(m->keys[i], "__class_name__") == 0) {
                NvObject* cn = m->values[i].obj;
                if (cn && cn->ob_type == NVStr_Type &&
                    strcmp(((NVStr*)cn)->value, want) == 0)
                    match = 1;
                break;
            }
        }
    }
    create_bool(&out, match);
    return out.obj;
}

// ── Exception handling ────────────────────────────────────────────────────────

// Separate NIR try depth counter (no setjmp involved).
static int nv_nir_try_depth = 0;

// ── Builtin exception constructors ───────────────────────────────────────────
// Called as: ValueError(msg_obj) -> NvObject*  (NIR call expression)

static NvObject* nir_make_exception(const char* type_name, NvObject* msg_obj) {
    const char* s = "";
    if (msg_obj && msg_obj->ob_type == NVStr_Type)
        s = ((NVStr*)msg_obj)->value;
    Value out = {NULL};
    nv_create_exception(&out, type_name, s);
    return out.obj;
}

NvObject* ValueError(NvObject* msg)      { return nir_make_exception("ValueError",     msg); }
NvObject* TypeError(NvObject* msg)       { return nir_make_exception("TypeError",      msg); }
NvObject* RuntimeError(NvObject* msg)    { return nir_make_exception("RuntimeError",   msg); }
NvObject* IndexError(NvObject* msg)      { return nir_make_exception("IndexError",     msg); }
NvObject* KeyError(NvObject* msg)        { return nir_make_exception("KeyError",       msg); }
NvObject* AttributeError(NvObject* msg)  { return nir_make_exception("AttributeError", msg); }
NvObject* NameError(NvObject* msg)       { return nir_make_exception("NameError",      msg); }
NvObject* AssertionError(NvObject* msg)  { return nir_make_exception("AssertionError", msg); }
NvObject* Error(NvObject* msg)           { return nir_make_exception("Error",          msg); }

void nv_throw(NvObject* exc_obj) {
    Value exc = {exc_obj};

    // `throw "boom"` throws a plain value, not an Error. nv_throw_exception casts
    // the object to NVError and reads ->message/->traceback, which for a string is
    // whatever follows the struct: the uncaught case printed "str: boom" and then
    // died with SIGSEGV. Wrapping gives the catch path and the uncaught path the
    // same object.
    if (exc.obj && !is_exception(&exc)) {
        const char* msg = "thrown value";
        if (exc.obj->ob_type == NVStr_Type && ((NVStr*)exc.obj)->value)
            msg = ((NVStr*)exc.obj)->value;
        Value wrapped = {NULL};
        nv_create_exception(&wrapped, "Error", msg);
        if (wrapped.obj) { exc = wrapped; nv_incref(exc.obj); }
    }

    if (nv_nir_try_depth > 0) {
        // Inside a NIR try block: save exception without longjmp so execution
        // continues linearly until nv_catch_check is reached after the body.
        nv_save_exception(&exc);
    } else {
        nv_throw_exception(&exc);
    }
}

// NIR try/catch uses flag-based error tracking (no setjmp).
// nv_try_push increments a NIR-specific depth counter rather than pushing a
// real setjmp frame. This lets nv_throw save the exception without longjmp-ing
// to an uninitialized buffer — execution continues linearly and the catch check
// runs after the try body completes.
void nv_try_push(void) {
    ++nv_nir_try_depth;
}

void nv_try_pop(void) {
    if (nv_nir_try_depth > 0) --nv_nir_try_depth;
    // Don't clear the exception here — nv_catch_check consults it and the
    // catch body may need e.message. Exception is cleared at the end of the
    // catch block (or by the next nv_try_pop if no catch matched).
}

NvObject* nv_catch_check(NvObject* type_name_obj) {
    const char* name = NULL;
    if (type_name_obj && type_name_obj->ob_type == NVStr_Type)
        name = ((NVStr*)type_name_obj)->value;
    Value out = {NULL};
    create_bool(&out, nv_exception_matches(name ? name : "Error"));
    return out.obj;
}

// A runtime error raised by the CORE (e.g. a tensor index out of range) that the language
// can observe: it creates the exception of the requested kind and follows the same path as
// `throw` — the flag inside a `try`, an abort with a traceback outside it. Unlike
// nv_raise_value_error, which prints and calls exit(1) on the spot and so bypasses the
// language's own error handling.
void nv_raise_nir_error(const char* kind, const char* msg) {
    Value wrapped = {NULL};
    nv_create_exception(&wrapped, kind, msg);
    if (!wrapped.obj) return;
    nv_incref(wrapped.obj);

    if (nv_nir_try_depth > 0) nv_save_exception(&wrapped);
    else                      nv_throw_exception(&wrapped);
}

// Is an error pending? `nv_has_exception` is static inside exceptions.c, and "Error" is the
// runtime's catch-all — so `nv_exception_matches("Error")` answers exactly that question.
// The try body calls this after every statement: the handling is flag based (no longjmp),
// so without the check a `throw` in the middle of the body did not stop what followed it.
NvObject* nv_has_pending_error(void) {
    Value out = {NULL};
    create_bool(&out, nv_exception_matches("Error"));
    return out.obj;
}

// The pending error, as a value the catch clause can hold. The incref matters:
// nv_get_current_exception_into hands back the runtime's own reference, and the
// ownership pass frees the catch variable's value at its last use — reading `e`
// in `catch Error e { write(e); }` then read freed memory and segfaulted.
NvObject* nv_get_current_error(void) {
    Value out = {NULL};
    nv_get_current_exception_into(&out);
    if (out.obj) nv_incref(out.obj);
    return out.obj;
}

NvObject* nv_had_error(void) {
    Value tmp = {NULL};
    nv_get_current_exception_into(&tmp);
    Value out = {NULL};
    create_bool(&out, tmp.obj != NULL);
    return out.obj;
}

// A catch body TAKES OVER the error: the codegen stores the object here and clears the
// flag, so "an error is pending" inside the body means "this handler raised it". Without
// that, the first check of the body (which exists so a `propagate` in the middle of it
// stops the rest) saw the flag still set from the error just handled, skipped the whole
// body — including the `propagate` itself.
#define NV_HANDLER_ERROR_MAX 64
static NvObject* nv_outer_error_stack[NV_HANDLER_ERROR_MAX];
static NvObject* nv_handled_error = NULL;   // erro que o handler atual assume
static int       nv_handler_error_depth = 0;

void nv_push_handler_error(NvObject* err) {
    Value cur = {NULL};
    nv_get_current_exception_into(&cur);

    if (nv_handler_error_depth < NV_HANDLER_ERROR_MAX)
        nv_outer_error_stack[nv_handler_error_depth++] = cur.obj;
    nv_clear_current_exception();

    if (err) nv_incref(err);                // o slot segura o objeto enquanto o corpo roda
    nv_handled_error = err;
}

void nv_pop_handler_error(void) {
    if (nv_handled_error) nv_decref(nv_handled_error);
    nv_handled_error = (nv_handler_error_depth > 0)
                     ? nv_outer_error_stack[--nv_handler_error_depth]
                     : NULL;
}

// `propagate` re-throws the current handler's error (or the pending one, outside a catch).
// It used to call nv_rethrow_current_exception, i.e. the legacy setjmp handler that the NIR
// codegen never pushes: inside a nested `try` the error escaped the whole program instead
// of reaching the catch outside.
void nv_propagate(void) {
    Value exc = { nv_handled_error };
    if (!exc.obj) nv_get_current_exception_into(&exc);
    if (!exc.obj) return;                   // nada pendente: `propagate` não tem o que fazer

    if (nv_nir_try_depth > 0) nv_save_exception(&exc);
    else                      nv_throw_exception(&exc);
}

// Called at the end of the `try` (after the `finally`, which always runs). The handler
// cleared the flag when it took the error over, so anything pending here was raised inside
// a handler (a `propagate`, a fresh `throw`) or matched no catch, and belongs to the
// enclosing handler. With none outside it is an uncaught error and has to be reported —
// the unconditional clear this used to do swallowed it silently.
void nv_finish_try(void) {
    Value exc = {NULL};
    nv_get_current_exception_into(&exc);
    if (!exc.obj) return;

    if (nv_nir_try_depth > 0) return;       // o handler de fora vai olhar
    nv_clear_current_exception();
    nv_throw_exception(&exc);
}

int nv_had_error_i1(void) {
    Value tmp = {NULL};
    nv_get_current_exception_into(&tmp);
    return tmp.obj != NULL;
}

void nv_clear_error(void) {
    nv_clear_current_exception();
}

int nv_is_result_err_i1(NvObject* obj) {
    if (!obj) return 0;
    return obj->ob_type == NVResultErr_Type;
}

int nv_is_truthy_i1(NvObject* obj) {
    if (!obj) return 0;
    if (obj->ob_type == NVBool_Type) return ((NVBool*)obj)->value;
    return 1;
}

// ── Async / await ─────────────────────────────────────────────────────────────

NvObject* nv_await_fiber(NvObject* fut_obj) {
    Value future = {fut_obj}, out = {NULL};
    nv_await(&out, &future);
    return out.obj;
}

// ── Closures ──────────────────────────────────────────────────────────────────

// nv_create_closure_cN(fn_ref:NvObject*(NVStr), cap0..capN-1)
// Resolves the closure function symbol (__closure_fn_N, compiled as an
// ordinary def with individual !llvm.ptr params) via dlsym — the final link
// exports __closure_fn_* symbols (-Wl,--export-dynamic-symbol) — snapshots the
// captured values into cells, and builds the runtime closure handle. One
// variant per capture count (the runtime-call re-declaration cannot serve
// variadic arities).
#define NV_CREATE_CLOSURE(N)                                                  \
    NvObject* nv_create_closure_c##N(NvObject* fn_ref,                        \
                                     NvObject* c0, NvObject* c1,              \
                                     NvObject* c2, NvObject* c3,              \
                                     NvObject* c4, NvObject* c5,              \
                                     NvObject* c6, NvObject* c7) {            \
        (void)c0; (void)c1; (void)c2; (void)c3;                               \
        (void)c4; (void)c5; (void)c6; (void)c7;                               \
        if (!fn_ref || fn_ref->ob_type != NVStr_Type) return NULL;            \
        const char* name = ((NVStr*)fn_ref)->value;                           \
        void* fn = dlsym(RTLD_DEFAULT, name);                                 \
        if (!fn) {                                                            \
            fprintf(stderr, "nv_create_closure: symbol '%s' not found\n",     \
                    name);                                                    \
            return NULL;                                                      \
        }                                                                     \
        Value** cells = NULL;                                                 \
        if (N > 0) {                                                          \
            cells = (Value**)malloc(sizeof(Value*) * (size_t)N);              \
            Value* cv[8];                                                     \
            cv[0] = &(Value){c0}; cv[1] = &(Value){c1};                       \
            cv[2] = &(Value){c2}; cv[3] = &(Value){c3};                       \
            cv[4] = &(Value){c4}; cv[5] = &(Value){c5};                       \
            cv[6] = &(Value){c6}; cv[7] = &(Value){c7};                       \
            for (int i = 0; i < N; ++i) cells[i] = nv_closure_cell_new(cv[i]); \
        }                                                                     \
        Value out = {NULL};                                                   \
        create_closure_with_captures(&out, fn, cells, N);                     \
        return out.obj;                                                       \
    }

NV_CREATE_CLOSURE(0)
NV_CREATE_CLOSURE(1)
NV_CREATE_CLOSURE(2)
NV_CREATE_CLOSURE(3)
NV_CREATE_CLOSURE(4)
NV_CREATE_CLOSURE(5)
NV_CREATE_CLOSURE(6)
NV_CREATE_CLOSURE(7)
NV_CREATE_CLOSURE(8)

// nv_invoke_closure_N(closure:NvObject*, a0..aN-1)
// Calls the closure handle: unboxed args are dispatched to the compiled
// closure function through its captured-function arity (see
// call_closure_indirect in lang/closures.c). One variant per arity so each
// has a fixed module-level signature (the runtime-call re-declaration cannot
// serve variadic arities).
#define NV_INVOKE_CLOSURE(N)                                                  \
    NvObject* nv_invoke_closure_##N(NvObject* clo,                            \
                                    NvObject* a0, NvObject* a1,               \
                                    NvObject* a2, NvObject* a3,               \
                                    NvObject* a4, NvObject* a5,               \
                                    NvObject* a6, NvObject* a7) {             \
        (void)a0; (void)a1; (void)a2; (void)a3;                               \
        (void)a4; (void)a5; (void)a6; (void)a7;                               \
        NvObject* args[8];                                                    \
        NvObject** ap = args;                                                 \
        if (N > 0) *ap++ = a0;                                                \
        if (N > 1) *ap++ = a1;                                                \
        if (N > 2) *ap++ = a2;                                                \
        if (N > 3) *ap++ = a3;                                                \
        if (N > 4) *ap++ = a4;                                                \
        if (N > 5) *ap++ = a5;                                                \
        if (N > 6) *ap++ = a6;                                                \
        if (N > 7) *ap++ = a7;                                                \
        Value cval = {clo}, out = {NULL};                                     \
        call_closure_indirect(&cval, args, N, &out);                          \
        return out.obj;                                                       \
    }

NV_INVOKE_CLOSURE(0)
NV_INVOKE_CLOSURE(1)
NV_INVOKE_CLOSURE(2)
NV_INVOKE_CLOSURE(3)
NV_INVOKE_CLOSURE(4)
NV_INVOKE_CLOSURE(5)
NV_INVOKE_CLOSURE(6)
NV_INVOKE_CLOSURE(7)
NV_INVOKE_CLOSURE(8)

// ── Interface dispatch ────────────────────────────────────────────────────────
// A call through an interface-typed value: the interface proves the method EXISTS, the
// class behind the value is a run-time fact (every class that says `implements I`), so
// the codegen cannot pick __method_<Class>_<name> itself as it does for a class-typed
// receiver. The instance carries its class in __class_name__ (see NewExprNode's codegen)
// and the class methods are exported by the final link
// (-Wl,--export-dynamic-symbol=__method_*), so the lookup is a dlsym of the mangled
// name — the same mechanism the closure bridges above use. The compiled method takes
// (self, a0..aN-1) as individual pointers, hence one bridge per arity.
//
// This needs the LINK step (the default mode and -b): the method symbols must be in the
// dynamic table of the program. In the REPL the module is JIT'd, so its symbols are not
// in the process's dynamic table and the lookup fails — the same limitation the closure
// bridges above have.
typedef NvObject* (*NvMethod0)(NvObject*);
typedef NvObject* (*NvMethod1)(NvObject*, NvObject*);
typedef NvObject* (*NvMethod2)(NvObject*, NvObject*, NvObject*);
typedef NvObject* (*NvMethod3)(NvObject*, NvObject*, NvObject*, NvObject*);
typedef NvObject* (*NvMethod4)(NvObject*, NvObject*, NvObject*, NvObject*, NvObject*);
typedef NvObject* (*NvMethod5)(NvObject*, NvObject*, NvObject*, NvObject*, NvObject*,
                               NvObject*);
typedef NvObject* (*NvMethod6)(NvObject*, NvObject*, NvObject*, NvObject*, NvObject*,
                               NvObject*, NvObject*);
typedef NvObject* (*NvMethod7)(NvObject*, NvObject*, NvObject*, NvObject*, NvObject*,
                               NvObject*, NvObject*, NvObject*);
typedef NvObject* (*NvMethod8)(NvObject*, NvObject*, NvObject*, NvObject*, NvObject*,
                               NvObject*, NvObject*, NvObject*, NvObject*);

// The receiver's class name, as stored by the `new` codegen; NULL when the value is not
// a class instance (a map that never went through `new`, or a non-object).
static void* nv_lookup_method(NvObject* self, NvObject* name_ref) {
    if (!self || !name_ref || name_ref->ob_type != NVStr_Type) return NULL;
    Value obj = {self}, cls = {NULL};
    nv_object_get_field(&cls, &obj, "__class_name__");
    if (!cls.obj || cls.obj->ob_type != NVStr_Type) return NULL;
    const char* class_name  = ((NVStr*)cls.obj)->value;
    const char* method_name = ((NVStr*)name_ref)->value;
    if (!class_name || !method_name) return NULL;

    char symbol[512];
    snprintf(symbol, sizeof(symbol), "__method_%s_%s", class_name, method_name);
    void* fn = dlsym(RTLD_DEFAULT, symbol);
    if (!fn)
        fprintf(stderr, "nv_dispatch_method: symbol '%s' not found\n", symbol);
    return fn;
}

// nv_dispatch_method_N(self, name, a0..aN-1): `self` is the receiver, `name` the method
// as a boxed str; the declared arguments follow with their real arity.
#define NV_DISPATCH_METHOD(N, PARAMS, ARGS)                                   \
    NvObject* nv_dispatch_method_##N PARAMS {                                 \
        void* fn = nv_lookup_method(self, name);                              \
        if (!fn) return NULL;                                                 \
        return ((NvMethod##N)fn) ARGS;                                        \
    }

NV_DISPATCH_METHOD(0, (NvObject* self, NvObject* name), (self))
NV_DISPATCH_METHOD(1, (NvObject* self, NvObject* name, NvObject* a0),
                   (self, a0))
NV_DISPATCH_METHOD(2, (NvObject* self, NvObject* name, NvObject* a0, NvObject* a1),
                   (self, a0, a1))
NV_DISPATCH_METHOD(3, (NvObject* self, NvObject* name, NvObject* a0, NvObject* a1,
                       NvObject* a2),
                   (self, a0, a1, a2))
NV_DISPATCH_METHOD(4, (NvObject* self, NvObject* name, NvObject* a0, NvObject* a1,
                       NvObject* a2, NvObject* a3),
                   (self, a0, a1, a2, a3))
NV_DISPATCH_METHOD(5, (NvObject* self, NvObject* name, NvObject* a0, NvObject* a1,
                       NvObject* a2, NvObject* a3, NvObject* a4),
                   (self, a0, a1, a2, a3, a4))
NV_DISPATCH_METHOD(6, (NvObject* self, NvObject* name, NvObject* a0, NvObject* a1,
                       NvObject* a2, NvObject* a3, NvObject* a4, NvObject* a5),
                   (self, a0, a1, a2, a3, a4, a5))
NV_DISPATCH_METHOD(7, (NvObject* self, NvObject* name, NvObject* a0, NvObject* a1,
                       NvObject* a2, NvObject* a3, NvObject* a4, NvObject* a5,
                       NvObject* a6),
                   (self, a0, a1, a2, a3, a4, a5, a6))
NV_DISPATCH_METHOD(8, (NvObject* self, NvObject* name, NvObject* a0, NvObject* a1,
                       NvObject* a2, NvObject* a3, NvObject* a4, NvObject* a5,
                       NvObject* a6, NvObject* a7),
                   (self, a0, a1, a2, a3, a4, a5, a6, a7))

// ── Threads ───────────────────────────────────────────────────────────────────
// There is no thread type in the language: spawn() returns a small integer id into a
// fixed table of live threads and join() takes that id. Running the body is exactly the
// closure call the codegen already emits (nv_invoke_closure_0), just on another pthread.
// Sharing a VALUE between threads is safe because the reference count is atomic
// (nv_arc_inc/nv_arc_dec); what a program does with the CONTENTS of a shared value is
// still its own business — there is no data-race checking (IMPLEMENTATION_FLOW item 3).
#define NV_MAX_THREADS 64

typedef struct {
    pthread_t tid;
    int       live;
    NvObject* closure;   // held while the thread runs
    NvObject* result;    // whatever the body returned, owned by the slot until join
} NvThreadSlot;

static NvThreadSlot    g_threads[NV_MAX_THREADS];
static pthread_mutex_t g_threads_lock = PTHREAD_MUTEX_INITIALIZER;

static void* nv_thread_trampoline(void* raw) {
    NvThreadSlot* slot = (NvThreadSlot*)raw;
    slot->result = nv_invoke_closure_0(slot->closure, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL);
    nv_decref(slot->closure);
    slot->closure = NULL;
    return NULL;
}

// spawn(body) -> the slot id, or -1 when there is no room / no thread.
NvObject* nv_thread_spawn(NvObject* closure) {
    if (!closure) return nv_box_int(-1);
    int slot = -1;
    pthread_mutex_lock(&g_threads_lock);
    for (int i = 0; i < NV_MAX_THREADS; i++)
        if (!g_threads[i].live) { slot = i; break; }
    if (slot >= 0) {
        g_threads[slot].live    = 1;
        g_threads[slot].result  = NULL;
        g_threads[slot].closure = closure;
    }
    pthread_mutex_unlock(&g_threads_lock);
    if (slot < 0) return nv_box_int(-1);

    nv_incref(closure);   // the thread owns it while it runs
    if (pthread_create(&g_threads[slot].tid, NULL, nv_thread_trampoline,
                       &g_threads[slot]) != 0) {
        nv_decref(closure);
        g_threads[slot].closure = NULL;
        g_threads[slot].live    = 0;
        return nv_box_int(-1);
    }
    return nv_box_int(slot);
}

// join(id) -> what the body returned (owned by the caller), or nothing for a bad id.
// pthread_join is also the synchronization point that makes the result visible.
NvObject* nv_thread_join(NvObject* id_obj) {
    int slot = obj_to_i32(id_obj);
    if (slot < 0 || slot >= NV_MAX_THREADS || !g_threads[slot].live) return NULL;
    pthread_join(g_threads[slot].tid, NULL);
    NvObject* result = g_threads[slot].result;
    g_threads[slot].result = NULL;
    g_threads[slot].live   = 0;
    return result;
}

// ── Parallel loops (@[optimize(parallelize)]) ─────────────────────────────────
// The body of a loop whose iterations are independent, called once per chunk with (start,
// end). The chunking lives here rather than in the IR because it is runtime policy: how many
// workers exist and how big a chunk is are facts about the machine, not about the program.
//
// The caller is responsible for the independence check (the checker does it, the same one
// @vectorize uses) — this bridge cannot see the body. Closures capture BY VALUE, so a body
// that writes a captured variable would not propagate the write; that is why a loop that is
// not independent must never reach here.
#define NV_PAR_WORKERS_MAX 8

typedef struct {
    NvObject* closure;
    int64_t   start;
    int64_t   end;
} NvParallelChunk;

static void* nv_parallel_trampoline(void* raw) {
    NvParallelChunk* c = (NvParallelChunk*)raw;
    NvObject* start = nv_box_int(c->start);
    NvObject* end   = nv_box_int(c->end);
    NvObject* out = nv_invoke_closure_2(c->closure, start, end, NULL, NULL,
                                        NULL, NULL, NULL, NULL);
    if (out)   nv_decref(out);       // the body's own value is not used here
    if (start) nv_decref(start);
    if (end)   nv_decref(end);
    nv_decref(c->closure);           // this thread's own reference
    return NULL;
}

// Run [0, total) through the closure, chunk by chunk, on several threads. The closure is
// invoked as (start, end) and every iteration belongs to exactly one chunk, so the result is
// the serial loop's result — that is the contract, and the reason the caller must have
// checked independence first.
//
// Workers: the host's cpu count, capped at NV_PAR_WORKERS_MAX and at the free slots of the
// shared thread table, so a program that already spawns cannot have its budget taken away. If
// no thread can be created the work still happens on this thread: a failed spawn must never
// turn into silently missing iterations.
NvObject* nv_parallel_for(NvObject* closure, NvObject* total_obj) {
    if (!closure) return nv_box_int(-1);
    int64_t total = obj_to_i32(total_obj);
    if (total <= 0) return nv_box_int(0);

#ifdef _WIN32
    long cpus = nv_cpu_count();
#else
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    int  workers = (cpus > 1) ? (int)cpus : 1;
    if (workers > NV_PAR_WORKERS_MAX)  workers = NV_PAR_WORKERS_MAX;
    if ((int64_t)workers > total)      workers = (int)total;

    int free_slots = 0;
    pthread_mutex_lock(&g_threads_lock);
    for (int i = 0; i < NV_MAX_THREADS; i++) if (!g_threads[i].live) free_slots++;
    pthread_mutex_unlock(&g_threads_lock);
    if (workers > free_slots) workers = free_slots;
    if (workers < 1) workers = 1;

    NvParallelChunk chunks[NV_PAR_WORKERS_MAX];
    pthread_t       tids[NV_PAR_WORKERS_MAX];
    int             started = 0;

    const int64_t per = (total + workers - 1) / workers;
    for (int i = 0; i < workers; i++) {
        int64_t start = (int64_t)i * per;
        if (start >= total) break;
        int64_t end = start + per;
        if (end > total) end = total;
        chunks[i].closure = closure;
        chunks[i].start   = start;
        chunks[i].end     = end;
        nv_incref(closure);                     // the thread owns a reference while it runs
        if (pthread_create(&tids[i], NULL, nv_parallel_trampoline, &chunks[i]) == 0) {
            started = i + 1;
        } else {
            nv_decref(closure);
            break;
        }
    }

    // Whatever could not be given to a thread runs here, in order, so no iteration is lost.
    for (int i = started; i < workers; i++) {
        int64_t start = (int64_t)i * per;
        if (start >= total) break;
        int64_t end = start + per;
        if (end > total) end = total;
        NvObject* a = nv_box_int(start);
        NvObject* b = nv_box_int(end);
        NvObject* out = nv_invoke_closure_2(closure, a, b, NULL, NULL, NULL, NULL, NULL, NULL);
        if (out) nv_decref(out);
        if (a)   nv_decref(a);
        if (b)   nv_decref(b);
    }

    for (int i = 0; i < started; i++) pthread_join(tids[i], NULL);
    return nv_box_int(0);
}

// ── Channels ──────────────────────────────────────────────────────────────────
// chan() returns an id into a fixed table of live channels, like spawn does for threads.
// A channel is an unbounded FIFO with the two operations a worker/consumer pair needs:
// send appends (and takes a reference of its own, so the sender keeps owning what it
// passed) and recv blocks while the channel is empty, handing the reference over to the
// receiver. That hand-over is what makes the pattern deterministic: every message is
// received exactly once, whatever order the threads finish in.
#define NV_MAX_CHANNELS 64

typedef struct NvChanMsg {
    NvObject*         value;
    struct NvChanMsg* next;
} NvChanMsg;

typedef struct {
    int             live;
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    NvChanMsg*      head;
    NvChanMsg*      tail;
} NvChanSlot;

static NvChanSlot      g_channels[NV_MAX_CHANNELS];
static pthread_mutex_t g_channels_lock = PTHREAD_MUTEX_INITIALIZER;

// chan() -> channel id, or -1 when there is no room.
NvObject* nv_channel_new(void) {
    int slot = -1;
    pthread_mutex_lock(&g_channels_lock);
    for (int i = 0; i < NV_MAX_CHANNELS; i++)
        if (!g_channels[i].live) { slot = i; break; }
    if (slot >= 0) {
        NvChanSlot* c = &g_channels[slot];
        c->live  = 1;
        c->head  = c->tail = NULL;
        pthread_mutex_init(&c->lock, NULL);
        pthread_cond_init(&c->not_empty, NULL);
    }
    pthread_mutex_unlock(&g_channels_lock);
    return nv_box_int(slot);
}

// send(channel, value) -> nothing. The channel holds a reference of its own.
NvObject* nv_channel_send(NvObject* id_obj, NvObject* value) {
    int slot = obj_to_i32(id_obj);
    if (slot < 0 || slot >= NV_MAX_CHANNELS || !g_channels[slot].live) return NULL;
    NvChanSlot* c = &g_channels[slot];
    NvChanMsg* msg = malloc(sizeof *msg);
    if (!msg) return NULL;
    nv_incref(value);
    msg->value = value;
    msg->next  = NULL;
    pthread_mutex_lock(&c->lock);
    if (c->tail) c->tail->next = msg; else c->head = msg;
    c->tail = msg;
    pthread_cond_signal(&c->not_empty);
    pthread_mutex_unlock(&c->lock);
    return NULL;
}

// recv(channel) -> the oldest message, waiting for one while the channel is empty.
NvObject* nv_channel_recv(NvObject* id_obj) {
    int slot = obj_to_i32(id_obj);
    if (slot < 0 || slot >= NV_MAX_CHANNELS || !g_channels[slot].live) return NULL;
    NvChanSlot* c = &g_channels[slot];
    pthread_mutex_lock(&c->lock);
    while (!c->head) pthread_cond_wait(&c->not_empty, &c->lock);
    NvChanMsg* msg = c->head;
    c->head = msg->next;
    if (!c->head) c->tail = NULL;
    pthread_mutex_unlock(&c->lock);
    NvObject* value = msg->value;   // the reference the channel was holding
    free(msg);
    return value;
}

// ── Narval builtin functions (NIR ABI wrappers) ───────────────────────────────
// Called as: callee(NvObject* arg) -> NvObject*
// (NIR calls builtins by their Narval name, not nv_* name)

NvObject* nv_write_bridge(NvObject* obj) {
    Value v = {obj};
    nv_write(&v);
    return NULL;
}

NvObject* nv_str_builtin(NvObject* obj) {
    Value v = {obj}, out = {NULL};
    nv_str_convert(&out, &v);
    return out.obj;
}

NvObject* nv_int_builtin(NvObject* obj) {
    Value v = {obj}, out = {NULL};
    nv_int_convert(&out, &v);
    return out.obj;
}

NvObject* nv_float_builtin(NvObject* obj) {
    Value v = {obj}, out = {NULL};
    nv_float_convert(&out, &v);
    return out.obj;
}

// json_field(obj, key, default): the value of a top-level key of a JSON object,
// as a runtime value. Strings come back unescaped, numbers as their raw text (so
// int()/float() convert them) and true/false as booleans; a null or missing key
// yields `default`, which is handed back with a reference of its own so the
// caller can drop the temporary it passed. Nested objects/arrays are skipped
// while looking for the key.
NvObject* nv_json_field_builtin(NvObject* json, NvObject* key, NvObject* fallback) {
    Value out = {NULL};
    if (!json || !key || json->ob_type != NVStr_Type || key->ob_type != NVStr_Type) {
        nv_raise_type_error("json_field() expects a JSON string and a key string");
        return NULL;
    }
    const char* s = ((NVStr*)json)->value;
    const char* k = ((NVStr*)key)->value;
    if (!s || !k) {
        nv_incref(fallback);
        return fallback;
    }

    const size_t klen = strlen(k);
    const char* p = s;
    int depth = 0;
    while (*p) {
        if (*p == '"') {
            const char* start = ++p;
            while (*p && *p != '"') p++;
            const size_t len = (size_t)(p - start);
            if (*p == '"') p++;
            const char* q = p;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
            if (*q == ':' && depth == 1 && len == klen && strncmp(start, k, klen) == 0) {
                q++;
                while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
                if (*q == '"') {
                    q++;
                    char* buf = (char*)malloc(strlen(q) + 1);
                    if (!buf) { nv_incref(fallback); return fallback; }
                    size_t n = 0;
                    while (*q && *q != '"') {
                        if (*q == '\\' && *(q + 1)) {
                            q++;
                            if (*q == 'n') buf[n++] = '\n';
                            else if (*q == 't') buf[n++] = '\t';
                            else if (*q == 'r') buf[n++] = '\r';
                            else buf[n++] = *q;
                        } else {
                            buf[n++] = *q;
                        }
                        q++;
                    }
                    buf[n] = '\0';
                    create_str(&out, buf);
                    free(buf);
                    return out.obj;
                }
                if (strncmp(q, "true", 4) == 0)  { create_bool(&out, 1); return out.obj; }
                if (strncmp(q, "false", 5) == 0) { create_bool(&out, 0); return out.obj; }
                if (strncmp(q, "null", 4) == 0)  { nv_incref(fallback); return fallback; }
                const char* e = q;
                while (*e && (isdigit((unsigned char)*e) || *e == '-' || *e == '+' ||
                              *e == '.' || *e == 'e' || *e == 'E')) e++;
                if (e == q) {
                    nv_incref(fallback);
                    return fallback;
                }
                char* num = (char*)malloc((size_t)(e - q) + 1);
                if (!num) { nv_incref(fallback); return fallback; }
                memcpy(num, q, (size_t)(e - q));
                num[e - q] = '\0';
                create_str(&out, num);
                free(num);
                return out.obj;
            }
            continue;
        }
        if (*p == '{' || *p == '[') { depth++; p++; continue; }
        if (*p == '}' || *p == ']') { depth--; p++; continue; }
        p++;
    }
    nv_incref(fallback);
    return fallback;
}

NvObject* nv_bool_builtin(NvObject* obj) {
    Value v = {obj}, out = {NULL};
    nv_bool_convert(&out, &v);
    return out.obj;
}

NvObject* nv_char_builtin(NvObject* obj) {
    Value v = {obj}, out = {NULL};
    nv_char_convert(&out, &v);
    return out.obj;
}

NvObject* nv_exit_builtin(NvObject* obj) {
    int code = obj ? obj_to_i32(obj) : 0;
    exit(code);
    return NULL;
}

// `read([prompt])` — the checker accepts read as a String-returning builtin, but
// the codegen emitted the bare name, which resolved to libc read(2) and segfaulted
// the moment it ran. Marshal through nv_read and box the line as a string.
NvObject* nv_read_builtin(NvObject* prompt) {
    const char* p = NULL;
    if (prompt && prompt->ob_type == NVStr_Type) p = ((NVStr*)prompt)->value;
    char* s = nv_read(p);
    Value out = {NULL};
    create_str(&out, s ? s : "");
    free(s);
    return out.obj;
}

// NIR entry point: OS starts with RSP%16==0, but LLVM's main.start prologue
// assumes RSP%16==8 (called via CALL). This stub subtracts 8 to fix alignment.
#ifdef _WIN32
// Windows the other way round: the PE entry point belongs to the CRT (which initialises the
// runtime library the generated code calls into), and the CRT calls main. So main is the
// trampoline into the narval top-level code, which lives in main.start. There is no
// RSP offset to fix here — the CRT calls main like any other function — but main.start
// still expects the SysV/Windows "called normally" alignment, hence the sub before the call.
__asm__(
    ".weak main.start\n"
    ".globl main\n"
    "main:\n"
    "    sub $8, %rsp\n"
    "    call main.start\n"
    "    ret\n"
);
#else
__asm__(
    ".weak main.start\n"
    ".globl _narval_entry\n"
    "_narval_entry:\n"
    "    sub $8, %rsp\n"
    "    jmp main.start\n"
);
#endif /* _WIN32 */

// Keeps a value alive across a store into a container: the statement's own reference
// to it is released right after, which would leave the container pointing at freed
// memory. Exposed as a symbol because the codegen emits a call to it.
NvObject* nv_incref_bridge(NvObject* obj) {
    nv_incref(obj);
    return obj;
}

// Some(x): same shape as nv_make_ok. The codegen emits this name for the constructor.
NvObject* nv_make_some(NvObject* val) {
    Value inner = {val}, out = {NULL};
    create_option_some(&out, &inner);
    return out.obj;
}
