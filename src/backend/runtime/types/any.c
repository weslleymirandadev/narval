#include "backend/runtime/nv_runtime.h"
#include <string.h>

void create_any(Value* out, Value v) {
    if (!out) return;
    *out = v;
    out->type = TAG_ANY;
    // Preservar type_info se existir
    if (v.type_info) {
        out->type_info = v.type_info;
    }
}

int any_has(Value* self, const char* key) {
    if (!self || self->type != TAG_ANY) return 0;
    Value v = *self;
    if (v.prototype == map_prototype && v.type == TAG_MAP) {
        Map* m = (Map*)(intptr_t)v.value;
        for (int i = 0; i < m->size; i++) {
            if (m->keys[i] && strcmp(m->keys[i], key) == 0) return 1;
        }
    }
    return 0;
}

void any_get(Value* out, Value* self, const char* key) {
    *out = (Value){0};
    if (!self || self->type != TAG_ANY) return;
    Value v = *self;

    if (v.prototype == map_prototype && v.type == TAG_MAP) {
        Map* m = (Map*)(intptr_t)v.value;
        for (int i = 0; i < m->size; i++) {
            if (m->keys[i] && strcmp(m->keys[i], key) == 0) {
                *out = m->values[i];
                out->type = TAG_ANY;  // mantém dinâmico
                return;
            }
        }
    }
}

void any_set(Value* self, const char* key, Value value) {
    if (!self || self->type != TAG_ANY) return;
    Value v = *self;

    // Se for map, usa map_set
    if (v.prototype == map_prototype && v.type == TAG_MAP) {
        Map* m = (Map*)(intptr_t)v.value;
        MapVTable* vt = (MapVTable*)v.prototype;
        vt->set(m, key, value);
        return;
    }

    // Caso contrário, converte para map
    Value new_map;
    create_map(&new_map);
    map_set_method(&new_map, key, value);
    *self = new_map;
    self->type = TAG_ANY;
}

void any_get_index(Value* out, Value* self, int index) {
    *out = (Value){0};
    if (!self || self->type != TAG_ANY) return;
    Value v = *self;

    if ((v.prototype == vector_prototype && v.type == TAG_VECTOR) ||
        (v.prototype == array_prototype && v.type == TAG_ARRAY)) {
        Vector* vec = (Vector*)(intptr_t)v.value;
        if (index >= 0 && index < vec->size) {
            *out = vec->elements[index];
            out->type = TAG_ANY;
        }
    }
}