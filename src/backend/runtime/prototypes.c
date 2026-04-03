#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================= */
/*                    PROTOTYPES - LEGADO ADAPTADO             */
/* ============================================================= */

// Declarações externas para evitar conflitos
extern void* string_prototype;
extern void* array_prototype;
extern void* vector_prototype;
extern void* map_prototype;

// Tabela de prototypes para funções de string (legado)
static void* string_prototype_table[] = {
    (void*)0,  // string_to_upper_case (implementado em value_utils.c)
    (void*)0,  // string_replace
    (void*)0,  // string_includes
    NULL
};

// Tabela de prototypes para funções de array (legado)
static void* array_prototype_table[] = {
    (void*)0,  // array_push
    (void*)0,  // array_pop
    NULL
};

// Tabela de prototypes para funções de vector (legado)
static void* vector_prototype_table[] = {
    (void*)0,  // vector_push
    (void*)0,  // vector_pop
    (void*)0,  // vector_get
    (void*)0,  // vector_set
    NULL
};

// Tabela de prototypes para funções de map (legado)
static void* map_prototype_table[] = {
    (void*)0,  // map_get
    (void*)0,  // map_set
    NULL
};

// Inicializar prototypes (função legado adaptada)
void init_prototypes(void) {
    // Criar prototypes básicos se não existirem
    if (!string_prototype) {
        string_prototype = string_prototype_table;
    }
    if (!array_prototype) {
        array_prototype = array_prototype_table;
    }
    if (!vector_prototype) {
        vector_prototype = vector_prototype_table;
    }
    if (!map_prototype) {
        map_prototype = map_prototype_table;
    }
}

// Obter prototype de tipo (função legado adaptada)
void* get_type_prototype(int32_t type_id) {
    switch (type_id) {
        case NV_STR_BASE:
            return string_prototype;
        case NV_ARRAY_BASE:
            return array_prototype;
        case NV_VECTOR_BASE:
            return vector_prototype;
        case NV_MAP_BASE:
            return map_prototype;
        default:
            return NULL;
    }
}

// Verificar se tipo tem prototype (função legado adaptada)
int has_prototype(int32_t type_id) {
    return get_type_prototype(type_id) != NULL;
}

// Limpar prototypes (função legado adaptada)
void cleanup_prototypes(void) {
    string_prototype = NULL;
    array_prototype = NULL;
    vector_prototype = NULL;
    map_prototype = NULL;
}
