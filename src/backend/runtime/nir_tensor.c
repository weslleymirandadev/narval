// nir_tensor.c — NIR bridge to the existing tensor runtime API.
// Converts between NvObject* (LLVM) and Value* (C API) conventions.

#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"
#include <stdint.h>

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
