#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================= */
/*                    REGISTRO DE TIPOS                         */
/* ============================================================= */

// Registro global de tipos
static TypeRegistry type_registry_storage = {NULL, 0, 0};
TypeRegistry* global_type_registry = &type_registry_storage;

// Inicializar registro de tipos
void init_type_registry(void) {
    if (global_type_registry->types == NULL) {
        global_type_registry->capacity = 32;
        global_type_registry->types = (TypeInfo**)calloc(
            global_type_registry->capacity, 
            sizeof(TypeInfo*)
        );
        global_type_registry->count = 0;
    }
}

// Registrar um novo tipo customizado
int32_t register_custom_type(TypeInfo* type_info) {
    if (!type_info) return -1;
    
    init_type_registry();
    
    // Verificar se já existe tipo com mesmo nome
    for (int i = 0; i < global_type_registry->count; i++) {
        if (global_type_registry->types[i] &&
            strcmp(global_type_registry->types[i]->type_name, type_info->type_name) == 0) {
            return global_type_registry->types[i]->type_id;
        }
    }
    
    // Expandir se necessário
    if (global_type_registry->count >= global_type_registry->capacity) {
        global_type_registry->capacity *= 2;
        global_type_registry->types = (TypeInfo**)realloc(
            global_type_registry->types,
            global_type_registry->capacity * sizeof(TypeInfo*)
        );
    }
    
    // Atribuir ID único (começando de TAG_CUSTOM + 1)
    int32_t new_id = TAG_CUSTOM + global_type_registry->count + 1;
    type_info->type_id = new_id;
    
    // Adicionar ao registro
    global_type_registry->types[global_type_registry->count] = type_info;
    global_type_registry->count++;
    
    return new_id;
}

// Obter informações de um tipo por ID
TypeInfo* get_type_info(int32_t type_id) {
    if (type_id < TAG_INT || type_id > TAG_CUSTOM) {
        return NULL;
    }
    
    // Tipos builtin não estão no registro
    if (type_id <= TAG_ANY) {
        return NULL; // Tipos builtin não têm TypeInfo completo
    }
    
    // Buscar tipo customizado
    init_type_registry();
    int32_t index = type_id - TAG_CUSTOM - 1;
    if (index >= 0 && index < global_type_registry->count) {
        return global_type_registry->types[index];
    }
    
    return NULL;
}

// Obter informações de um tipo por nome
TypeInfo* get_type_info_by_name(const char* name) {
    if (!name) return NULL;
    
    init_type_registry();
    for (int i = 0; i < global_type_registry->count; i++) {
        if (global_type_registry->types[i] &&
            strcmp(global_type_registry->types[i]->type_name, name) == 0) {
            return global_type_registry->types[i];
        }
    }
    
    return NULL;
}

// Verificar se um tipo é válido
int is_valid_type(int32_t type) {
    if (type >= TAG_INT && type <= TAG_ANY) {
        return 1; // Tipos builtin são sempre válidos
    }
    if (type >= TAG_CUSTOM) {
        return get_type_info(type) != NULL;
    }
    return 0;
}

// Obter nome do tipo como string
const char* get_type_name(int32_t type) {
    switch (type) {
        case TAG_INT:    return "int";
        case TAG_FLOAT:  return "float";
        case TAG_BOOL:   return "bool";
        case TAG_STR:    return "str";
        case TAG_ARRAY:  return "array";
        case TAG_VECTOR: return "vector";
        case TAG_MAP:    return "map";
        case TAG_TUPLE:  return "tuple";
        case TAG_ANY:    return "any";
        case TAG_CUSTOM: return "custom";
        default: {
            TypeInfo* info = get_type_info(type);
            if (info) return info->type_name;
            return "unknown";
        }
    }
}
