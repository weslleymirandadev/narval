#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Definição real de NVType_Type para resolver undefined references
NvTypeObject* NVType_Type = NULL;

/* ============================================================= */
/*                    INICIALIZAÇÃO DO RUNTIME DO NARVAL        */
/* ============================================================= */

// Tipos builtin globais
NvTypeObject* NVInt_Type = NULL;
NvTypeObject* NVFloat_Type = NULL;
NvTypeObject* NVBool_Type = NULL;
NvTypeObject* NVStr_Type = NULL;
NvTypeObject* NVArray_Type = NULL;
NvTypeObject* NVVector_Type = NULL;
NvTypeObject* NVMap_Type = NULL;
NvTypeObject* NVTuple_Type = NULL;
NvTypeObject* NVObject_Type = NULL;

// Prototypes globais para tipos builtin (legado compatibilidade)
void* string_prototype = NULL;
void* array_prototype = NULL;
void* vector_prototype = NULL;
void* map_prototype = NULL;

// Declaração da função de inicialização de classes builtin
void initialize_builtin_classes(void);

// Registrar símbolos globais (para compatibilidade com compilador)
void register_global_init(void) {
    static int initialized = 0;
    if (initialized) {
        return;
    }
    initialized = 1;

    nv_type_system_init();

    // Usar novo sistema de classes builtin
    initialize_builtin_classes();
}

/* ============================================================= */
/*              ACESSO A CAMPOS DE OBJETOS (CLASSES)             */
/* ============================================================= */

void nv_object_get_field(Value* out, Value* self, const char* key) {
    if (!out) return;
    if (!self || !self->obj || !key) { out->obj = NULL; return; }

    NVMap* map = (NVMap*)self->obj;
    for (int i = 0; i < map->size; i++) {
        if (map->keys[i] && strcmp(map->keys[i], key) == 0) {
            *out = map->values[i];
            return;
        }
    }
    out->obj = NULL;
}

void nv_object_set_field(Value* self, const char* key, Value* val) {
    if (!self || !self->obj || !key || !val) return;

    NVMap* map = (NVMap*)self->obj;
    for (int i = 0; i < map->size; i++) {
        if (map->keys[i] && strcmp(map->keys[i], key) == 0) {
            map->values[i] = *val;
            return;
        }
    }
    if (map->size >= map->capacity) {
        int new_cap = map->capacity == 0 ? 4 : map->capacity * 2;
        map->keys   = (char**)realloc(map->keys,   new_cap * sizeof(char*));
        map->values = (Value*)realloc(map->values,  new_cap * sizeof(Value));
        map->capacity = new_cap;
    }
    map->keys[map->size]   = strdup(key);
    map->values[map->size] = *val;
    map->size++;
}
