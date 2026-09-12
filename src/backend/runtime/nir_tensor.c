// nir_tensor.c — NIR bridge to the existing tensor runtime API.
// Converts between NvObject* (LLVM) and Value* (C API) conventions.

#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// nv_tensor_fill_nd(fill, ndim, d0..d7) -> NvObject*
Value nv_tensor_fill_nd(int64_t fill_val, int64_t ndim,
                         int64_t d0, int64_t d1, int64_t d2, int64_t d3,
                         int64_t d4, int64_t d5, int64_t d6, int64_t d7) {
    nv_tensor_init_type();
    int64_t dims[8] = {d0, d1, d2, d3, d4, d5, d6, d7};
    int32_t ndim32 = (int32_t)ndim;
    if (fill_val == 0)
        return nv_tensor_zeros(NV_FLOAT_BASE, ndim32, dims);
    else
        return nv_tensor_ones(NV_FLOAT_BASE, ndim32, dims);
}

// Bridge: nv_tensor_data_ptr_bridge(NvObject*) -> void*
// The raw element buffer, for the vectorized-loop codegen: it does the loads and stores
// itself on this pointer, so the loop never boxes an element.
void* nv_tensor_data_ptr_bridge(NvObject* t) {
    Value v = {t};
    return nv_tensor_data_ptr(&v);
}

// Bridge: nv_tensor_add_bridge(NvObject*, NvObject*) -> NvObject*
NvObject* nv_tensor_add_bridge(NvObject* a, NvObject* b) {
    Value av = {a}, bv = {b};
    Value r = nv_tensor_add(&av, &bv);
    return r.obj;
}

// Bridge: nv_tensor_mul_bridge(NvObject*, NvObject*) -> NvObject*
NvObject* nv_tensor_mul_bridge(NvObject* a, NvObject* b) {
    Value av = {a}, bv = {b};
    Value r = nv_tensor_mul(&av, &bv);
    return r.obj;
}

// Bridge: nv_tensor_matmul_bridge(NvObject*, NvObject*) -> NvObject*
NvObject* nv_tensor_matmul_bridge(NvObject* a, NvObject* b) {
    Value av = {a}, bv = {b};
    Value r = nv_tensor_matmul(&av, &bv);
    return r.obj;
}

// Bridge: nv_tensor_transpose_bridge(NvObject*) -> NvObject*
NvObject* nv_tensor_transpose_bridge(NvObject* a) {
    Value av = {a};
    Value r = nv_tensor_transpose(&av);
    return r.obj;
}

// Bridge: nv_tensor_item_bridge(NvObject*) -> NvObject*
NvObject* nv_tensor_item_bridge(NvObject* a) {
    Value av = {a};
    Value r = nv_tensor_item(&av);
    return r.obj;
}

// Bridge: nv_tensor_tolist_bridge(NvObject*) -> NvObject*
NvObject* nv_tensor_tolist_bridge(NvObject* a) {
    Value av = {a};
    Value r = nv_tensor_tolist(&av);
    return r.obj;
}

// ── Per-attribute bridges (for NIR codegen with fixed function signatures) ─

NvObject* nv_tensor_get_shape_bridge(NvObject* a) {
    Value av = {a};
    int32_t nd = nv_tensor_ndim(&av);
    Value out = {NULL};
    create_array(&out, nd);
    if (!out.obj) return NULL;
    NVArray* arr = (NVArray*)out.obj;
    for (int32_t i = 0; i < nd; i++) {
        int64_t d = nv_tensor_dim(&av, i);
        Value v = {NULL};
        create_int(&v, (int32_t)d);
        arr->elements[i] = v;
    }
    return out.obj;
}

NvObject* nv_tensor_get_ndim_bridge(NvObject* a) {
    Value av = {a};
    int32_t nd = nv_tensor_ndim(&av);
    Value r = {NULL};
    create_int(&r, nd);
    return r.obj;
}

NvObject* nv_tensor_get_dtype_bridge(NvObject* a) {
    (void)a;
    // Always return "float64" for now — NVTensor is opaque in headers
    Value r = {NULL}; create_str(&r, "float64"); return r.obj;
}

NvObject* nv_tensor_nelem_bridge(NvObject* a) {
    Value av = {a};
    int64_t n = nv_tensor_nelem(&av);
    Value r = {NULL};
    create_int(&r, (int32_t)n);
    return r.obj;
}

NvObject* nv_tensor_reshape_bridge(NvObject* a, int64_t ndim,
                                     int64_t d0, int64_t d1, int64_t d2, int64_t d3,
                                     int64_t d4, int64_t d5, int64_t d6, int64_t d7) {
    Value av = {a};
    int64_t new_shape[8] = {d0, d1, d2, d3, d4, d5, d6, d7};
    int64_t total = 1;
    for (int64_t i = 0; i < ndim; i++) total *= new_shape[i];
    if (total != nv_tensor_nelem(&av)) {
        Value bad = {NULL}; return bad.obj;
    }
    // Extract raw data buffer based on dtype
    int is_float = 0;
    // Check if data has any float values by iterating... actually,
    // always use nv_tensor_from_data with the original data ptr.
    // But the dtype info is in the opaque NVTensor.
    // Use the nelem and data ptr directly:
    int64_t nelem = nv_tensor_nelem(&av);
    void* data = nv_tensor_data_ptr(&av);
    if (!data) { Value bad = {NULL}; return bad.obj; }
    // Check if data looks like float by seeing if it's NV_FLOAT_BASE
    // We don't know dtype, but nv_tensor_data_ptr returns the internal buffer.
    // Since we always create with NV_FLOAT_BASE in nv_tensor_fill_nd,
    // and Tensor({ints}) creates int tensors, we need to handle both.
    // Try to get item to detect type
    Value item = nv_tensor_item(&av);
    is_float = (item.obj && item.obj->ob_type == NVFloat_Type) ? 1 : 0;

    if (is_float) {
        double* buf = (double*)malloc(nelem * sizeof(double));
        if (!buf) { Value bad = {NULL}; return bad.obj; }
        memcpy(buf, data, nelem * sizeof(double));
        Value r = nv_tensor_from_data(NV_FLOAT_BASE, (int32_t)ndim, new_shape, buf);
        free(buf);
        return r.obj;
    } else {
        int32_t* buf = (int32_t*)malloc(nelem * sizeof(int32_t));
        if (!buf) { Value bad = {NULL}; return bad.obj; }
        memcpy(buf, data, nelem * sizeof(int32_t));
        Value r = nv_tensor_from_data(NV_INT_BASE, (int32_t)ndim, new_shape, buf);
        free(buf);
        return r.obj;
    }
}

// ── Tensor from flat array (bridge for `Tensor({1,2,3})`) ────────
// nv_tensor_from_flat_array(flat_array, ndim, d0..d7) -> NvObject*
// flat_array is an NVArray containing all elements in row-major order.
// dtype is inferred: if any element is float, use float; else int.
NvObject* nv_tensor_from_flat_array(NvObject* flat, int64_t ndim,
                                     int64_t d0, int64_t d1, int64_t d2, int64_t d3,
                                     int64_t d4, int64_t d5, int64_t d6, int64_t d7) {
    if (!flat || flat->ob_type != NVArray_Type) {
        Value bad = {NULL}; return bad.obj;
    }
    NVArray* src = (NVArray*)flat;
    int64_t shape[8] = {d0, d1, d2, d3, d4, d5, d6, d7};

    // Check if any element is float → use float dtype
    int use_float = 0;
    for (int32_t i = 0; i < src->size; i++) {
        if (src->elements[i].obj && src->elements[i].obj->ob_type == NVFloat_Type) {
            use_float = 1;
            break;
        }
    }

    if (use_float) {
        double* buf = (double*)malloc(src->size * sizeof(double));
        if (!buf) { Value bad = {NULL}; return bad.obj; }
        for (int32_t i = 0; i < src->size; i++) {
            Value* e = &src->elements[i];
            if (e->obj && e->obj->ob_type == NVFloat_Type)
                buf[i] = ((NVFloat*)e->obj)->value;
            else if (e->obj && e->obj->ob_type == NVInt_Type)
                buf[i] = (double)((NVInt*)e->obj)->value;
            else
                buf[i] = 0.0;
        }
        Value r = nv_tensor_from_data(NV_FLOAT_BASE, (int32_t)ndim, shape, buf);
        free(buf);
        return r.obj;
    } else {
        int32_t* buf = (int32_t*)malloc(src->size * sizeof(int32_t));
        if (!buf) { Value bad = {NULL}; return bad.obj; }
        for (int32_t i = 0; i < src->size; i++) {
            Value* e = &src->elements[i];
            if (e->obj && e->obj->ob_type == NVInt_Type)
                buf[i] = ((NVInt*)e->obj)->value;
            else
                buf[i] = 0;
        }
        Value r = nv_tensor_from_data(NV_INT_BASE, (int32_t)ndim, shape, buf);
        free(buf);
        return r.obj;
    }
}

// ── nv_box_tensor — box a flat tensor data buffer into a Value ─────
// nv_box_tensor(ptr_i64, ndim_i64, elem_size_i64, dtype_i64, d0_i64..d7_i64)
//   -> Value
// Boxes a flat MLIR memref data buffer into a runtime NVTensor value
// (copying). elem_size is the MLIR element byte width (1/2/4/8); dtype is
// NV_INT_BASE(1) or NV_FLOAT_BASE(2) — the NVTensor storage is int32/double,
// so narrower sources are widened element-wise (f32→double, i8/i16→int32).
// Unsupported combos (i64, f16/bf16) return a null Value.
Value nv_box_tensor(int64_t ptr_int, int64_t ndim, int64_t elem_size,
                     int64_t dtype,
                     int64_t d0, int64_t d1, int64_t d2, int64_t d3,
                     int64_t d4, int64_t d5, int64_t d6, int64_t d7) {
    nv_tensor_init_type();
    void* data = (void*)(intptr_t)ptr_int;
    int64_t dims[8] = {d0, d1, d2, d3, d4, d5, d6, d7};
    if (ndim < 0 || ndim > 8) { Value bad = {NULL}; return bad; }

    int64_t nelem = 1;
    for (int64_t i = 0; i < ndim && i < 8; i++)
        nelem *= dims[i];
    if (nelem < 0 || nelem > 1000000000) { Value bad = {NULL}; return bad; }

    if (nelem == 0 || !data)
        return nv_tensor_zeros((int32_t)dtype, (int32_t)ndim, dims);

    if (dtype == NV_INT_BASE) {
        // NVTensor int storage is int32 — widen i8/i16, memcpy i32.
        int32_t* tmp = (int32_t*)malloc((size_t)nelem * sizeof(int32_t));
        if (!tmp) { Value bad = {NULL}; return bad; }
        if (elem_size == 4) {
            memcpy(tmp, data, (size_t)nelem * sizeof(int32_t));
        } else if (elem_size == 1) {
            const int8_t* src = (const int8_t*)data;
            for (int64_t i = 0; i < nelem; i++) tmp[i] = (int32_t)src[i];
        } else if (elem_size == 2) {
            const int16_t* src = (const int16_t*)data;
            for (int64_t i = 0; i < nelem; i++) tmp[i] = (int32_t)src[i];
        } else {
            free(tmp); // i64: NVInt storage is int32 — refuse to truncate.
            Value bad = {NULL}; return bad;
        }
        Value v = nv_tensor_from_data(NV_INT_BASE, (int32_t)ndim, dims, tmp);
        free(tmp);
        return v;
    }

    // NVTensor float storage is double — widen f32, memcpy f64.
    double* tmp = (double*)malloc((size_t)nelem * sizeof(double));
    if (!tmp) { Value bad = {NULL}; return bad; }
    if (elem_size == 4) {
        const float* src = (const float*)data;
        for (int64_t i = 0; i < nelem; i++) tmp[i] = (double)src[i];
    } else if (elem_size == 8) {
        memcpy(tmp, data, (size_t)nelem * sizeof(double));
    } else {
        free(tmp); // f16/bf16 boxing not implemented yet.
        Value bad = {NULL}; return bad;
    }
    Value v = nv_tensor_from_data(NV_FLOAT_BASE, (int32_t)ndim, dims, tmp);
    free(tmp);
    return v;
}

// ── Generic attribute access bridge ───────────────────────────────
//
// nv_getattr_bridge(NvObject*, NvObject* attr_str) -> NvObject*
// Handles tensor attributes (shape, ndim, dtype, T, etc.) and
// falls back to field access for maps/classes.
NvObject* nv_getattr_bridge(NvObject* obj, NvObject* attr_name) {
    if (!obj || !attr_name || attr_name->ob_type != NVStr_Type) {
        Value bad = {NULL}; return bad.obj;
    }
    const char* name = ((NVStr*)attr_name)->value;
    if (!name) { Value bad = {NULL}; return bad.obj; }

    // Check if this is a tensor
    if (obj->ob_type == NVTensor_Type) {
        Value av = {obj};
        if (strcmp(name, "shape") == 0) {
            int32_t nd = nv_tensor_ndim(&av);
            Value out = {NULL};
            create_array(&out, nd);
            if (!out.obj) return NULL;
            NVArray* arr = (NVArray*)out.obj;
            for (int32_t i = 0; i < nd; i++) {
                int64_t d = nv_tensor_dim(&av, i);
                Value v = {NULL};
                create_int(&v, (int32_t)d);
                arr->elements[i] = v;
            }
            return out.obj;
        }
        if (strcmp(name, "ndim") == 0) {
            int32_t nd = nv_tensor_ndim(&av);
            Value r = {NULL};
            create_int(&r, nd);
            return r.obj;
        }
        if (strcmp(name, "dtype") == 0) {
            // NVTensor struct is opaque here; we always create with NV_FLOAT_BASE
            Value r = {NULL};
            create_str(&r, "float64");
            return r.obj;
        }
        if (strcmp(name, "nelem") == 0) {
            int64_t n = nv_tensor_nelem(&av);
            Value r = {NULL};
            create_int(&r, (int32_t)n);
            return r.obj;
        }
        if (strcmp(name, "T") == 0) {
            Value r = nv_tensor_transpose(&av);
            return r.obj;
        }
        if (strcmp(name, "item") == 0) {
            Value r = nv_tensor_item(&av);
            return r.obj;
        }
        if (strcmp(name, "tolist") == 0) {
            Value r = nv_tensor_tolist(&av);
            return r.obj;
        }
    }

    // Fallback: try map/class field access
    Value self = {obj};
    Value out = {NULL};
    Value key = {attr_name};
    nv_object_get_field(&out, &self, name);
    return out.obj;
}

// ── Reshape bridge (all args boxed as NvObject*) ─────────────────
// nv_tensor_reshape_boxed_bridge(obj, d0, d1, ..., d7) -> NvObject*
// Each dim is a boxed int (NVInt_Type); max 8 dims + obj = 9 args.
NvObject* nv_tensor_reshape_boxed_bridge(NvObject* a, NvObject* d0, NvObject* d1,
    NvObject* d2, NvObject* d3, NvObject* d4, NvObject* d5, NvObject* d6, NvObject* d7) {
    Value av = {a};
    if (!av.obj || av.obj->ob_type != NVTensor_Type) {
        Value bad = {NULL}; return bad.obj;
    }

    NvObject* dims_arr[8] = {d0, d1, d2, d3, d4, d5, d6, d7};
    int64_t shape[8];
    int ndim = 0;
    for (; ndim < 8; ndim++) {
        NvObject* d = dims_arr[ndim];
        if (!d || d->ob_type != NVInt_Type) break;
        shape[ndim] = (int64_t)((NVInt*)d)->value;
        if (shape[ndim] == 0) break; // stop at first zero dim
    }
    if (ndim == 0) { Value bad = {NULL}; return bad.obj; }

    int64_t total = 1;
    for (int i = 0; i < ndim; i++) total *= shape[i];
    if (total != nv_tensor_nelem(&av)) {
        Value bad = {NULL}; return bad.obj;
    }

    Value r = nv_tensor_from_data(NV_FLOAT_BASE, ndim, shape, nv_tensor_data_ptr(&av));
    return r.obj;
}
