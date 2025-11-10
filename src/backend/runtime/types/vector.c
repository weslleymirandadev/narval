#include "backend/runtime/rph_runtime.h"

extern void* vector_prototype;

void create_vector(Value* out, int capacity) {
    Vector* vec = (Vector*)malloc(sizeof(Vector));
    if (!vec) {
        fprintf(stderr, "FATAL: malloc failed in create_vector\n");
        exit(1);
    }
    vec->size = 0;
    vec->capacity = capacity > 0 ? capacity : 4;
    vec->elements = (Value*)malloc(sizeof(Value) * vec->capacity);
    if (!vec->elements && vec->capacity > 0) {
        free(vec);
        fprintf(stderr, "FATAL: malloc failed in create_vector elements\n");
        exit(1);
    }

    out->type = TAG_VECTOR;
    out->value = (int64_t)(intptr_t)vec;
    out->prototype = vector_prototype;
}

void vector_push_method(Value* out, Value* self, const Value* value) {
    if (!self || self->type != TAG_VECTOR) {
        out->type = 0; out->value = 0; out->prototype = NULL;
        return;
    }
    Vector* vec = (Vector*)(intptr_t)self->value;
    VectorVTable* vt = (VectorVTable*)self->prototype;
    vt->push(vec, value ? *value : (Value){0});
    out->type = 0; out->value = 0; out->prototype = NULL;
}

void vector_pop_method(Value* out, Value* self) {
    if (!self || self->type != TAG_VECTOR) {
        out->type = 0; out->value = 0; out->prototype = NULL;
        return;
    }
    Vector* vec = (Vector*)(intptr_t)self->value;
    VectorVTable* vt = (VectorVTable*)self->prototype;
    *out = vt->pop(vec);
}

void vector_get_method(Value* out, Value* self, int index) {
    if (!self || self->type != TAG_VECTOR) {
        out->type = 0; out->value = 0; out->prototype = NULL;
        return;
    }
    Vector* vec = (Vector*)(intptr_t)self->value;
    VectorVTable* vt = (VectorVTable*)self->prototype;
    *out = vt->get(vec, index);
}

void vector_set_method(Value* self, int index, const Value* value) {
    if (!self || self->type != TAG_VECTOR) return;
    Vector* vec = (Vector*)(intptr_t)self->value;
    VectorVTable* vt = (VectorVTable*)self->prototype;
    vt->set(vec, index, value ? *value : (Value){0});
}

void vector_push_impl(Vector* v, Value val) {
    if (v->size == v->capacity) {
        v->capacity = v->capacity == 0 ? 4 : v->capacity * 2;
        v->elements = (Value*)realloc(v->elements, sizeof(Value) * v->capacity);
    }
    v->elements[v->size++] = val;
}

Value vector_pop_impl(Vector* v) {
    if (v->size == 0) return (Value){0};
    return v->elements[--v->size];
}

Value vector_get_impl(Vector* v, int i) {
    if (i >= 0 && i < v->size) return v->elements[i];
    return (Value){0};
}

void vector_set_impl(Vector* v, int i, Value val) {
    if (i < 0) return;
    if (i >= v->capacity) {
        int cap = v->capacity == 0 ? 4 : v->capacity;
        while (i >= cap) cap *= 2;
        v->elements = (Value*)realloc(v->elements, sizeof(Value) * cap);
        v->capacity = cap;
    }
    if (i >= v->size) v->size = i + 1;
    v->elements[i] = val;
}