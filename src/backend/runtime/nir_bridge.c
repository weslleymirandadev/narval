// nir_bridge.c — Value-returning bridge functions for the NIR (MLIR) pipeline.
//
// In the NIR pipeline, !narval.value lowers to !llvm.ptr, and values are passed
// as NvObject* (the inner pointer from Value.obj).  These bridges wrap the
// existing output-parameter runtime functions and expose the value-returning ABI
// that the NIR-generated LLVM IR expects.

#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

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
    create_int(&out, (int32_t)v);
    return out.obj;
}

NvObject* nv_box_float(double v) {
    Value out = {NULL};
    create_float(&out, v);
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

// ── Comparison ───────────────────────────────────────────────────────────────

NvObject* nv_value_eq(NvObject* a, NvObject* b) {
    Value va = {a}, vb = {b}, out = {NULL};
    create_bool(&out, nv_value_cmp(&va, &vb) == 0);
    return out.obj;
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
    return out.obj;
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
    const char* key = NULL;
    if (key_obj->ob_type == NVStr_Type)
        key = ((NVStr*)key_obj)->value;
    if (!key) return;
    Value map = {map_obj}, val = {val_obj};
    nv_object_set_field(&map, key, &val);
}

// ── Iteration ─────────────────────────────────────────────────────────────────

int32_t nv_len(NvObject* obj) {
    if (!obj) return 0;
    Value v = {obj};
    return nv_get_iterable_length(&v);
}

NvObject* nv_get_at(NvObject* arr_obj, int32_t idx) {
    if (!arr_obj) return NULL;
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

NvObject* nv_make_none(void) {
    Value out = {NULL};
    create_option_none(&out);
    return out.obj;
}

NvObject* nv_value_is_some_or_ok(NvObject* obj) {
    Value out = {NULL};
    int ok = obj &&
             (obj->ob_type == NVOptionSome_Type ||
              obj->ob_type == NVResultOk_Type);
    create_bool(&out, ok);
    return out.obj;
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

NvObject* nv_get_current_error(void) {
    Value out = {NULL};
    nv_get_current_exception_into(&out);
    return out.obj;
}

NvObject* nv_had_error(void) {
    Value tmp = {NULL};
    nv_get_current_exception_into(&tmp);
    Value out = {NULL};
    create_bool(&out, tmp.obj != NULL);
    return out.obj;
}

void nv_propagate(void) {
    nv_rethrow_current_exception();
}

// ── Async / await ─────────────────────────────────────────────────────────────

NvObject* nv_await_fiber(NvObject* fut_obj) {
    Value future = {fut_obj}, out = {NULL};
    nv_await(&out, &future);
    return out.obj;
}

// ── Closures ──────────────────────────────────────────────────────────────────

// Called as: nv_create_closure(NvObject* fn_ref, NvObject* cap0, ...)
// fn_ref is a string Value containing the MLIR symbol name of the closure function.
// Without a symbol table lookup, we create a placeholder closure.
NvObject* nv_create_closure(NvObject* fn_ref, ...) {
    (void)fn_ref;
    Value out = {NULL};
    create_closure(&out, NULL);
    return out.obj;
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

// NIR entry point: OS starts with RSP%16==0, but LLVM's main.start prologue
// assumes RSP%16==8 (called via CALL). This stub subtracts 8 to fix alignment.
__asm__(
    ".weak main.start\n"
    ".globl _narval_entry\n"
    "_narval_entry:\n"
    "    sub $8, %rsp\n"
    "    jmp main.start\n"
);
