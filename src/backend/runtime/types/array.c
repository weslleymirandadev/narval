#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <stdlib.h>

extern void* array_prototype;

void create_array(Value* out, int size) {
    Array* arr = (Array*)malloc(sizeof(Array));
    if (!arr) {
        fprintf(stderr, "FATAL: malloc failed in create_array\n");
        exit(1);
    }
    arr->size = size;
    arr->capacity = size;
    arr->elements = (Value*)calloc(size, sizeof(Value));
    if (!arr->elements && size > 0) {
        free(arr);
        fprintf(stderr, "FATAL: calloc failed in create_array\n");
        exit(1);
    }

    out->type = TAG_ARRAY;
    out->value = (int64_t)(intptr_t)arr;
    out->prototype = array_prototype;
    out->type_info = NULL;
    out->flags = 0;
}

static Value value_clone(Value v) {
    if (v.type == TAG_STR) {
        char* s = (char*)(intptr_t)v.value;
        if (!s) return v;
        size_t len = strlen(s);
        char* dup = (char*)malloc(len + 1);
        if (!dup) return v; // fallback without cloning on OOM
        memcpy(dup, s, len + 1);
        Value out = v;
        out.value = (int64_t)(intptr_t)dup;
        return out;
    }
    return v;
}

Value array_get_index(Array* arr, int index) {
    if (index >= 0 && index < arr->size) {
        return arr->elements[index];
    }
    return (Value){0, 0, NULL};
}

void array_set_index(Array* arr, int index, Value value) {
    if (index >= 0 && index < arr->size) {
        arr->elements[index] = value_clone(value);
    }
}

void array_get_index_v(Value* out, Value* self, int index) {
    if (!out) return;
    if (!self) { out->type = 0; out->value = 0; out->prototype = NULL; return; }

    if (self->type == TAG_ARRAY) {
        Array* arr = (Array*)(intptr_t)self->value;
        *out = array_get_index(arr, index);
        return;
    }

    if (self->type == TAG_VECTOR) {
        Vector* vec = (Vector*)(intptr_t)self->value;
        VectorVTable* vt = (VectorVTable*)self->prototype;
        if (vt && vt->get) {
            *out = vt->get(vec, index);
        } else {
            out->type = 0; out->value = 0; out->prototype = NULL;
        }
        return;
    }

    out->type = 0; out->value = 0; out->prototype = NULL;
}

void array_set_index_v(Value* self, int index, const Value* value) {
    if (!self) return;
    if (self->type == TAG_ARRAY) {
        Array* arr = (Array*)(intptr_t)self->value;
        if (index >= 0) {
            if (index >= arr->capacity) {
                int newcap = arr->capacity > 0 ? arr->capacity : 1;
                while (index >= newcap) newcap *= 2;
                arr->elements = (Value*)realloc(arr->elements, sizeof(Value) * newcap);
                arr->capacity = newcap;
            }
            if (index >= arr->size) arr->size = index + 1;
            if (value) {
                array_set_index(arr, index, *value);
            }
        }
        return;
    }

    if (self->type == TAG_VECTOR) {
        Vector* vec = (Vector*)(intptr_t)self->value;
        VectorVTable* vt = (VectorVTable*)self->prototype;
        if (vt && vt->set && value) vt->set(vec, index, *value);
        return;
    }
}