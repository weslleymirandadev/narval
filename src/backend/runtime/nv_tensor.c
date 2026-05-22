#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "backend/runtime/prototypes.h"

//  NVTensor object 

typedef struct {
    NvObject_HEAD;
    int32_t  dtype;     // NV_FLOAT_BASE or NV_INT_BASE
    int32_t  ndim;
    int64_t* shape;     // heap-allocated array of ndim dimensions
    int64_t* strides;   // row-major strides in elements
    void*    data;      // raw buffer (double* or int32_t*)
    int64_t  nelem;     // total number of elements
} NVTensor;

// Global type object for NVTensor (initialised by nv_tensor_init_type).
NvTypeObject* NVTensor_Type = NULL;

//  Internal helpers 

static int64_t compute_nelem(int32_t ndim, const int64_t* shape) {
    int64_t n = 1;
    for (int32_t i = 0; i < ndim; ++i) n *= shape[i];
    return n;
}

static void compute_strides(int32_t ndim, const int64_t* shape, int64_t* strides) {
    // Row-major (C order)
    int64_t stride = 1;
    for (int32_t i = ndim - 1; i >= 0; --i) {
        strides[i] = stride;
        stride *= shape[i];
    }
}

//  Allocate an NVTensor (no data initialisation) 

static NVTensor* tensor_alloc(int32_t dtype, int32_t ndim, const int64_t* shape) {
    NVTensor* t = (NVTensor*)malloc(sizeof(NVTensor));
    if (!t) return NULL;
    t->ob_base.ob_type  = NVTensor_Type;
    t->ob_base.ref_count = 1;
    t->ob_base.flags     = 0;
    t->dtype = dtype;
    t->ndim  = ndim;
    t->shape   = (int64_t*)malloc(ndim * sizeof(int64_t));
    t->strides = (int64_t*)malloc(ndim * sizeof(int64_t));
    if (!t->shape || !t->strides) { free(t->shape); free(t->strides); free(t); return NULL; }
    memcpy(t->shape, shape, ndim * sizeof(int64_t));
    compute_strides(ndim, shape, t->strides);
    t->nelem = compute_nelem(ndim, shape);
    size_t elem_sz = (dtype == NV_INT_BASE) ? sizeof(int32_t) : sizeof(double);
    t->data = malloc(t->nelem * elem_sz);
    if (!t->data) { free(t->shape); free(t->strides); free(t); return NULL; }
    return t;
}

//  Wrap NVTensor in a Value* 

static Value tensor_to_value(NVTensor* t) {
    Value v;
    v.obj = (NvObject*)t;
    return v;
}

//  Helper: extract integer from a boxed Value 

int64_t nv_value_to_i64(Value* v) {
    if (!v || !v->obj) return 0;
    NvTypeObject* t = v->obj->ob_type;
    if (!t) return 0;
    if (t == NVInt_Type) {
        NVInt* i = (NVInt*)v->obj;
        return (int64_t)i->value;
    }
    if (t == NVFloat_Type) {
        NVFloat* f = (NVFloat*)v->obj;
        return (int64_t)f->value;
    }
    return 0;
}

//  Public API 

// Create a tensor filled with zeros.
// dtype: NV_FLOAT_BASE or NV_INT_BASE
// ndim:  number of dimensions
// shape: array of ndim int64_t dimensions
Value nv_tensor_zeros(int32_t dtype, int32_t ndim, const int64_t* shape) {
    NVTensor* t = tensor_alloc(dtype, ndim, shape);
    if (!t) { Value v; v.obj = NULL; return v; }
    size_t elem_sz = (dtype == NV_INT_BASE) ? sizeof(int32_t) : sizeof(double);
    memset(t->data, 0, t->nelem * elem_sz);
    return tensor_to_value(t);
}

// Create a tensor filled with ones.
Value nv_tensor_ones(int32_t dtype, int32_t ndim, const int64_t* shape) {
    NVTensor* t = tensor_alloc(dtype, ndim, shape);
    if (!t) { Value v; v.obj = NULL; return v; }
    if (dtype == NV_INT_BASE) {
        int32_t* p = (int32_t*)t->data;
        for (int64_t i = 0; i < t->nelem; ++i) p[i] = 1;
    } else {
        double* p = (double*)t->data;
        for (int64_t i = 0; i < t->nelem; ++i) p[i] = 1.0;
    }
    return tensor_to_value(t);
}

// Create a tensor from a flat data buffer (copied).
Value nv_tensor_from_data(int32_t dtype, int32_t ndim,
                           const int64_t* shape, const void* src_data) {
    NVTensor* t = tensor_alloc(dtype, ndim, shape);
    if (!t) { Value v; v.obj = NULL; return v; }
    size_t elem_sz = (dtype == NV_INT_BASE) ? sizeof(int32_t) : sizeof(double);
    memcpy(t->data, src_data, t->nelem * elem_sz);
    return tensor_to_value(t);
}

//  Scalar creation helpers (for Narval literal initialisation) 
// nv_tensor_scalar_f(x) wraps a double in a 0-D tensor.

Value nv_tensor_scalar_f(double x) {
    int64_t shape[1] = {1};
    NVTensor* t = tensor_alloc(NV_FLOAT_BASE, 1, shape);
    if (!t) { Value v; v.obj = NULL; return v; }
    ((double*)t->data)[0] = x;
    return tensor_to_value(t);
}

//  Element access 

static NVTensor* unwrap_tensor(Value* v) {
    if (!v || !v->obj) return NULL;
    if (v->obj->ob_type != NVTensor_Type) return NULL;
    return (NVTensor*)v->obj;
}

double nv_tensor_get_f(Value* v, int32_t ndim, const int64_t* idx) {
    NVTensor* t = unwrap_tensor(v);
    if (!t || t->dtype != NV_FLOAT_BASE) return 0.0;
    int64_t offset = 0;
    for (int32_t i = 0; i < ndim && i < t->ndim; ++i)
        offset += idx[i] * t->strides[i];
    return ((double*)t->data)[offset];
}

void nv_tensor_set_f(Value* v, int32_t ndim, const int64_t* idx, double val) {
    NVTensor* t = unwrap_tensor(v);
    if (!t || t->dtype != NV_FLOAT_BASE) return;
    int64_t offset = 0;
    for (int32_t i = 0; i < ndim && i < t->ndim; ++i)
        offset += idx[i] * t->strides[i];
    ((double*)t->data)[offset] = val;
}

//  Shape / info 

int32_t nv_tensor_ndim(Value* v) {
    NVTensor* t = unwrap_tensor(v); return t ? t->ndim : 0;
}
int64_t nv_tensor_dim(Value* v, int32_t axis) {
    NVTensor* t = unwrap_tensor(v);
    if (!t || axis < 0 || axis >= t->ndim) return 0;
    return t->shape[axis];
}
int64_t nv_tensor_nelem(Value* v) {
    NVTensor* t = unwrap_tensor(v); return t ? t->nelem : 0;
}
void* nv_tensor_data_ptr(Value* v) {
    NVTensor* t = unwrap_tensor(v); return t ? t->data : NULL;
}

//  Arithmetic (naive fallback — MLIR-generated versions override these) 

// Matrix multiply: C = A @ B   (2-D only, float only)
// Returns a new tensor; caller owns it.
Value nv_tensor_matmul(Value* a_v, Value* b_v) {
    NVTensor* A = unwrap_tensor(a_v);
    NVTensor* B = unwrap_tensor(b_v);
    Value bad; bad.obj = NULL;
    if (!A || !B) return bad;
    if (A->ndim != 2 || B->ndim != 2) {
        fprintf(stderr, "nv_tensor_matmul: only 2-D tensors supported\n");
        return bad;
    }
    if (A->shape[1] != B->shape[0]) {
        fprintf(stderr, "nv_tensor_matmul: shape mismatch (%lld != %lld)\n",
                (long long)A->shape[1], (long long)B->shape[0]);
        return bad;
    }
    int64_t M = A->shape[0], K = A->shape[1], N = B->shape[1];
    int64_t out_shape[2] = {M, N};
    NVTensor* C = tensor_alloc(NV_FLOAT_BASE, 2, out_shape);
    if (!C) return bad;
    double* a = (double*)A->data;
    double* b = (double*)B->data;
    double* c = (double*)C->data;
    memset(c, 0, M * N * sizeof(double));
    // Naive triple loop — MLIR path generates SIMD-optimised version
    for (int64_t i = 0; i < M; ++i)
        for (int64_t k = 0; k < K; ++k) {
            double aik = a[i * K + k];
            for (int64_t j = 0; j < N; ++j)
                c[i * N + j] += aik * b[k * N + j];
        }
    return tensor_to_value(C);
}

// Element-wise add
Value nv_tensor_add(Value* a_v, Value* b_v) {
    NVTensor* A = unwrap_tensor(a_v);
    NVTensor* B = unwrap_tensor(b_v);
    Value bad; bad.obj = NULL;
    if (!A || !B || A->nelem != B->nelem) return bad;
    NVTensor* C = tensor_alloc(A->dtype, A->ndim, A->shape);
    if (!C) return bad;
    if (A->dtype == NV_FLOAT_BASE) {
        double* a = (double*)A->data; double* b = (double*)B->data; double* c = (double*)C->data;
        for (int64_t i = 0; i < A->nelem; ++i) c[i] = a[i] + b[i];
    } else {
        int32_t* a = (int32_t*)A->data; int32_t* b = (int32_t*)B->data; int32_t* c = (int32_t*)C->data;
        for (int64_t i = 0; i < A->nelem; ++i) c[i] = a[i] + b[i];
    }
    return tensor_to_value(C);
}

// Element-wise subtract (fallback when NARVAL_USE_NIR is off)
Value nv_tensor_sub(Value* a_v, Value* b_v) {
    NVTensor* A = unwrap_tensor(a_v);
    NVTensor* B = unwrap_tensor(b_v);
    Value bad; bad.obj = NULL;
    if (!A || !B || A->nelem != B->nelem) return bad;
    NVTensor* C = tensor_alloc(A->dtype, A->ndim, A->shape);
    if (!C) return bad;
    if (A->dtype == NV_FLOAT_BASE) {
        double* a = (double*)A->data; double* b = (double*)B->data; double* c = (double*)C->data;
        for (int64_t i = 0; i < A->nelem; ++i) c[i] = a[i] - b[i];
    } else {
        int32_t* a = (int32_t*)A->data; int32_t* b = (int32_t*)B->data; int32_t* c = (int32_t*)C->data;
        for (int64_t i = 0; i < A->nelem; ++i) c[i] = a[i] - b[i];
    }
    return tensor_to_value(C);
}

// Element-wise multiply
Value nv_tensor_mul(Value* a_v, Value* b_v) {
    NVTensor* A = unwrap_tensor(a_v);
    NVTensor* B = unwrap_tensor(b_v);
    Value bad; bad.obj = NULL;
    if (!A || !B || A->nelem != B->nelem) return bad;
    NVTensor* C = tensor_alloc(A->dtype, A->ndim, A->shape);
    if (!C) return bad;
    if (A->dtype == NV_FLOAT_BASE) {
        double* a = (double*)A->data; double* b = (double*)B->data; double* c = (double*)C->data;
        for (int64_t i = 0; i < A->nelem; ++i) c[i] = a[i] * b[i];
    } else {
        int32_t* a = (int32_t*)A->data; int32_t* b = (int32_t*)B->data; int32_t* c = (int32_t*)C->data;
        for (int64_t i = 0; i < A->nelem; ++i) c[i] = a[i] * b[i];
    }
    return tensor_to_value(C);
}

// Element-wise scalar multiply
Value nv_tensor_scalar_mul(Value* a_v, double scalar) {
    NVTensor* A = unwrap_tensor(a_v);
    Value bad; bad.obj = NULL;
    if (!A) return bad;
    NVTensor* C = tensor_alloc(A->dtype, A->ndim, A->shape);
    if (!C) return bad;
    if (A->dtype == NV_FLOAT_BASE) {
        double* a = (double*)A->data; double* c = (double*)C->data;
        for (int64_t i = 0; i < A->nelem; ++i) c[i] = a[i] * scalar;
    }
    return tensor_to_value(C);
}

//  Type registration 

void nv_tensor_init_type(void) {
    if (NVTensor_Type) return;
    NVTensor_Type = (NvTypeObject*)calloc(1, sizeof(NvTypeObject));
    NVTensor_Type->tp_name      = "tensor";
    NVTensor_Type->tp_basicsize = sizeof(NVTensor);
}

//  Tensor print 
//
// Prints N-D tensors in a nested list format:
//   1-D:  [1, 2, 3]
//   2-D:  [           3-D and beyond: recursive, one row per line
//            [1, 2],
//            [3, 4],
//          ]

// Forward declaration
static void tensor_print_dim(const NVTensor* t, int dim,
                              int64_t offset, int indent, int newline_after);

static void print_indent(int n) {
    for (int i = 0; i < n; ++i) printf("  ");
}

static void print_scalar(const NVTensor* t, int64_t idx) {
    if (t->dtype == NV_FLOAT_BASE) {
        double v = ((double*)t->data)[idx];
        // Print without trailing zeros: 1.0 → "1", 1.5 → "1.5"
        if (v == (int64_t)v && v >= -1e15 && v <= 1e15)
            printf("%lld", (long long)v);
        else
            printf("%g", v);
    } else {
        printf("%d", ((int32_t*)t->data)[idx]);
    }
}

// dim: current dimension being printed
// offset: flat index of the first element in this slice
// indent: current indentation level (in 2-space units)
// newline_after: whether to print '\n' after the closing ']'
static void tensor_print_dim(const NVTensor* t, int dim,
                              int64_t offset, int indent, int newline_after) {
    int64_t size = t->shape[dim];
    int is_innermost = (dim == t->ndim - 1);

    if (is_innermost) {
        // Print inline: [e0, e1, ..., eN]
        printf("[");
        for (int64_t i = 0; i < size; ++i) {
            if (i) printf(", ");
            print_scalar(t, offset + i);
        }
        printf("]");
        if (newline_after) printf("\n");
    } else {
        // Stride for this dimension
        int64_t stride = 1;
        for (int d = dim + 1; d < t->ndim; ++d) stride *= t->shape[d];

        printf("[\n");
        for (int64_t i = 0; i < size; ++i) {
            print_indent(indent + 1);
            tensor_print_dim(t, dim + 1, offset + i * stride, indent + 1, 0);
            printf(",\n");
        }
        print_indent(indent);
        printf("]");
        if (newline_after) printf("\n");
    }
}

void nv_tensor_print(Value* v) {
    NVTensor* t = unwrap_tensor(v);
    if (!t) { printf("<invalid tensor>\n"); return; }
    if (t->ndim == 0 || t->nelem == 0) { printf("[]\n"); return; }
    tensor_print_dim(t, 0, 0, 0, 1);
}
