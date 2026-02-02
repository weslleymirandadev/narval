#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void* map_prototype;

void create_map(Value* out) {
    Map* m = (Map*)malloc(sizeof(Map));
    if (!m) {
        fprintf(stderr, "FATAL: malloc failed in create_map\n");
        exit(1);
    }
    m->size = 0;
    m->capacity = 4;
    m->keys = (char**)malloc(sizeof(char*) * 4);
    m->values = (Value*)malloc(sizeof(Value) * 4);
    if (!m->keys || !m->values) {
        free(m->keys); free(m->values); free(m);
        fprintf(stderr, "FATAL: malloc failed in create_map buffers\n");
        exit(1);
    }

    out->type = TAG_MAP;
    out->value = (int64_t)(intptr_t)m;
    out->prototype = map_prototype;
    out->type_info = NULL;
    out->flags = 0;
}

void map_get_method(Value* out, Value* self, const char* key) {
    if (!self || self->type != TAG_MAP || !key) {
        out->type = 0; out->value = 0; out->prototype = NULL;
        return;
    }
    Map* m = (Map*)(intptr_t)self->value;
    MapVTable* vt = (MapVTable*)self->prototype;
    *out = vt->get(m, key);
}

void map_set_method(Value* self, const char* key, Value value) {
    if (!self || self->type != TAG_MAP || !key) return;
    Map* m = (Map*)(intptr_t)self->value;
    MapVTable* vt = (MapVTable*)self->prototype;
    vt->set(m, key, value);
}

Value map_get_impl(Map* m, const char* key) {
    for (int i = 0; i < m->size; ++i) {
        if (strcmp(m->keys[i], key) == 0) return m->values[i];
    }
    return (Value){0};
}

void map_set_impl(Map* m, const char* key, Value val) {
    for (int i = 0; i < m->size; ++i) {
        if (strcmp(m->keys[i], key) == 0) {
            m->values[i] = val;
            return;
        }
    }
    if (m->size == m->capacity) {
        m->capacity = m->capacity == 0 ? 4 : m->capacity * 2;
        m->keys = (char**)realloc(m->keys, sizeof(char*) * m->capacity);
        m->values = (Value*)realloc(m->values, sizeof(Value) * m->capacity);
    }
    m->keys[m->size] = strdup(key);
    m->values[m->size] = val;
    ++m->size;
}