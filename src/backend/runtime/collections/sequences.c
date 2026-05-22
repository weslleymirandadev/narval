// collections/sequences.c — Array and vector operations.

#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

// ── Array ──────────────────────────────────────────────────────────────────

void array_get_index_v(Value* out, Value* self_arr, int32_t index) {
    if (out) memset(out, 0, sizeof(Value));
    if (!self_arr || !self_arr->obj) return;
    NVArray* a = (NVArray*)self_arr->obj;
    if (index < 0) index += a->size;
    if (index < 0 || index >= a->size) return;
    if (out) *out = a->elements[index];
}

void array_set_index_v(Value* self_arr, int32_t index, Value* elem) {
    if (!self_arr || !self_arr->obj || !elem) return;
    NVArray* a = (NVArray*)self_arr->obj;
    if (index < 0) index += a->size;
    if (index < 0 || index >= a->size) return;
    a->elements[index] = *elem;
}

void array_push_method(Array* a, Value val) {
    if (!a) return;
    if (a->size >= a->capacity) {
        int nc = a->capacity == 0 ? 4 : a->capacity * 2;
        a->elements = (Value*)realloc(a->elements, nc * sizeof(Value));
        a->capacity = nc;
    }
    a->elements[a->size++] = val;
}

Value array_pop_method(Array* a) {
    Value result = {0};
    if (!a || a->size == 0) return result;
    result = a->elements[--a->size];
    return result;
}

// ── Vector ─────────────────────────────────────────────────────────────────

void vector_push_method(Value* out, Value* self_vec, Value* elem) {
    (void)out;
    if (!self_vec || !self_vec->obj || !elem) return;
    NVVector* v = (NVVector*)self_vec->obj;
    if (v->size >= v->capacity) {
        int nc = v->capacity == 0 ? 4 : v->capacity * 2;
        Value* ne = (Value*)realloc(v->elements, nc * sizeof(Value));
        if (!ne) return;
        v->elements = ne; v->capacity = nc;
    }
    v->elements[v->size++] = *elem;
}

void vector_pop_method(Value* out, Value* self_vec) {
    if (out) memset(out, 0, sizeof(Value));
    if (!self_vec || !self_vec->obj) return;
    NVVector* v = (NVVector*)self_vec->obj;
    if (v->size == 0) return;
    if (out) *out = v->elements[--v->size];
    else v->size--;
}

void vector_get_method(Value* out, Value* self_vec, int32_t index) {
    if (out) memset(out, 0, sizeof(Value));
    if (!self_vec || !self_vec->obj) return;
    NVVector* v = (NVVector*)self_vec->obj;
    if (index < 0) index += v->size;
    if (index < 0 || index >= v->size) return;
    if (out) *out = v->elements[index];
}

void vector_set_method(Value* self_vec, int32_t index, Value* elem) {
    if (!self_vec || !self_vec->obj || !elem) return;
    NVVector* v = (NVVector*)self_vec->obj;
    if (index < 0) index += v->size;
    if (index < 0) return;
    if (index >= v->size) {
        int nc = v->capacity == 0 ? 4 : v->capacity;
        while (index >= nc) nc *= 2;
        if (nc > v->capacity) {
            Value* ne = (Value*)realloc(v->elements, nc * sizeof(Value));
            if (!ne) return;
            v->elements = ne; v->capacity = nc;
        }
        for (int i = v->size; i <= index; i++) memset(&v->elements[i], 0, sizeof(Value));
        v->size = index + 1;
    }
    v->elements[index] = *elem;
}

// ── Generic index set (dispatches to array or vector) ─────────────────────

void nv_set_at_index(Value* container, int32_t idx, Value* elem) {
    if (!container || !container->obj || !elem) return;
    NvObject* obj = container->obj;
    if (obj->ob_type == NVVector_Type) vector_set_method(container, idx, elem);
    else if (obj->ob_type == NVArray_Type) array_set_index_v(container, idx, elem);
}

int32_t nv_get_iterable_length(Value* self) {
    if (!self || !self->obj) return 0;
    NvObject* obj = self->obj;
    if (obj->ob_type == NVVector_Type || obj->ob_type == NVArray_Type)
        return ((NVArray*)obj)->size;
    if (obj->ob_type == NVStr_Type)
        return (int32_t)((NVStr*)obj)->len;
    return 0;
}

// ── Slice ──────────────────────────────────────────────────────────────────

#define NV_SLICE_NONE (-2147483648)

static void slice_push(NVVector* v, Value elem) {
    if (v->size >= v->capacity) {
        int nc = v->capacity == 0 ? 4 : v->capacity * 2;
        v->elements = (Value*)realloc(v->elements, nc * sizeof(Value));
        v->capacity = nc;
    }
    v->elements[v->size++] = elem;
}

void nv_collection_slice(Value* out, Value* self, int32_t start, int32_t stop, int32_t step) {
    if (out) memset(out, 0, sizeof(Value));
    if (!self || !self->obj) { create_vector(out, 0); return; }
    NvObject* obj = self->obj;
    Value* elems = NULL; int size = 0;
    if (obj->ob_type == NVVector_Type) { NVVector* v = (NVVector*)obj; elems = v->elements; size = v->size; }
    else if (obj->ob_type == NVArray_Type) { NVArray* a = (NVArray*)obj; elems = a->elements; size = a->size; }
    else { create_vector(out, 0); return; }

    if (step == NV_SLICE_NONE) step = 1;
    if (step == 0) { create_vector(out, 0); return; }

    if (start == NV_SLICE_NONE) start = step > 0 ? 0 : size - 1;
    else {
        if (start < 0) start += size;
        if (step > 0) { if (start < 0) start = 0; if (start > size) start = size; }
        else          { if (start < 0) start = -1; if (start >= size) start = size - 1; }
    }

    if (stop == NV_SLICE_NONE) stop = step > 0 ? size : -1;
    else {
        if (stop < 0) stop += size;
        if (step > 0) { if (stop < 0) stop = 0; if (stop > size) stop = size; }
        else          { if (stop < 0) stop = -1; if (stop >= size) stop = size - 1; }
    }

    create_vector(out, 4);
    NVVector* result = (NVVector*)out->obj;
    if (step > 0) { for (int i = start; i < stop; i += step) slice_push(result, elems[i]); }
    else          { for (int i = start; i > stop; i += step) slice_push(result, elems[i]); }
}
