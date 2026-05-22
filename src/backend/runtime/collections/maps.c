// collections/maps.c — Map (hash-map backed by parallel arrays) operations.

#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

Value map_get_method(Map* m, const char* key) {
    Value result = {0};
    if (!m || !key) return result;
    for (int i = 0; i < m->size; i++) {
        if (m->keys[i] && strcmp(m->keys[i], key) == 0)
            return m->values[i];
    }
    create_any(&result);
    return result;
}

void map_set_method(Map* m, const char* key, Value val) {
    if (!m || !key) return;
    for (int i = 0; i < m->size; i++) {
        if (m->keys[i] && strcmp(m->keys[i], key) == 0) { m->values[i] = val; return; }
    }
    if (m->size >= m->capacity) {
        int nc = m->capacity == 0 ? 4 : m->capacity * 2;
        m->keys   = (char**)realloc(m->keys,   nc * sizeof(char*));
        m->values = (Value*)realloc(m->values, nc * sizeof(Value));
        m->capacity = nc;
    }
    m->keys[m->size]   = strdup(key);
    m->values[m->size] = val;
    m->size++;
}
