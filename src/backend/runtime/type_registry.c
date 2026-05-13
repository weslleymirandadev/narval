#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================= */
/*                    SISTEMA DE TIPOS DO NARVAL                 */
/* ============================================================= */

// Implementação básica das funções do sistema de tipos
void nv_type_system_init(void) {
    if (!global_type_registry) {
        global_type_registry = (TypeRegistry*)calloc(1, sizeof(TypeRegistry));
        global_type_registry->capacity = 16;
        global_type_registry->types = (NvTypeObject**)calloc(16, sizeof(NvTypeObject*));
        global_type_registry->count = 0;
    }
}

NvTypeObject* nv_type_new(const char* name, NvTypeObject** bases, int bases_count) {
    NvTypeObject* type = (NvTypeObject*)calloc(1, sizeof(NvTypeObject));
    if (!type) return NULL;
    
    // Para o tipo 'type' itself, ob_type deve ser NULL para evitar referência circular
    type->ob_base.ob_type = (strcmp(name, "type") == 0) ? NULL : NVType_Type;
    type->ob_base.ref_count = 1;
    type->ob_base.flags = 0;
    
    type->tp_name = name ? strdup(name) : "unknown";
    type->tp_basicsize = sizeof(NvObject);
    type->tp_new = NULL;
    type->tp_dealloc = NULL;
    type->tp_as_number = NULL;
    type->tp_as_sequence = NULL;
    type->tp_as_mapping = NULL;
    type->tp_methods = NULL;
    type->tp_methods_count = 0;
    type->tp_bases = bases;
    type->tp_bases_count = bases_count;
    type->tp_metaclass = NULL;  // Remover dependência circular
    type->tp_cache = NULL;
    
    return type;
}

NvObject* nv_object_new(NvTypeObject* type, void* args, void* kwargs) {
    if (!type) return NULL;
    
    NvObject* obj = (NvObject*)calloc(1, type->tp_basicsize);
    if (!obj) return NULL;
    
    obj->ob_type = type;
    obj->ref_count = 1;
    obj->flags = 0;
    
    return obj;
}

NvTypeObject* nv_create_simple_class(const char* name) {
    return nv_type_new(name, NULL, 0);
}

/* ============================================================= */
/*                    REGISTRO DE TIPOS - NOVO SISTEMA       */
/* ============================================================= */

// Registro global de tipos
TypeRegistry* global_type_registry = NULL;

// Inicializar registro de tipos
void init_type_registry(void) {
    // Já inicializado em nv_type_system_init()
}

// Registrar tipo customizado (legado - adaptado)
int32_t register_custom_type(TypeInfo* type_info) {
    if (!type_info || !global_type_registry) return -1;
    
    // No novo sistema, criar NvTypeObject a partir de TypeInfo
    NvTypeObject* new_type = (NvTypeObject*)calloc(1, sizeof(NvTypeObject));
    if (!new_type) return -1;
    
    // Configurar tipo base
    new_type->ob_base.ob_type = NVType_Type;
    new_type->ob_base.ref_count = 1;
    new_type->ob_base.flags = 0;
    
    // Configurar metadados
    new_type->tp_name = type_info->type_name ? strdup(type_info->type_name) : "custom";
    new_type->tp_basicsize = sizeof(NVObject);
    new_type->tp_new = NULL;
    new_type->tp_dealloc = NULL;
    new_type->tp_as_number = NULL;
    new_type->tp_as_sequence = NULL;
    new_type->tp_as_mapping = NULL;
    new_type->tp_methods = NULL;
    new_type->tp_methods_count = 0;
    new_type->tp_bases = (NvTypeObject**)calloc(1, sizeof(NvTypeObject*));
    new_type->tp_bases[0] = NVObject_Type;
    new_type->tp_bases_count = 1;
    new_type->tp_metaclass = NVType_Type;
    new_type->tp_cache = type_info;  // Guardar referência ao TypeInfo
    
    // Adicionar ao registro
    if (global_type_registry->count >= global_type_registry->capacity) {
        global_type_registry->capacity *= 2;
        global_type_registry->types = (NvTypeObject**)realloc(
            global_type_registry->types,
            global_type_registry->capacity * sizeof(NvTypeObject*)
        );
    }
    
    int32_t type_id = NV_TYPE_BASE + global_type_registry->count;
    global_type_registry->types[global_type_registry->count++] = new_type;
    
    return type_id;
}

// Obter informações de tipo por ID
TypeInfo* get_type_info(int32_t type_id) {
    if (!global_type_registry || type_id < NV_TYPE_BASE) return NULL;
    
    int index = type_id - NV_TYPE_BASE;
    if (index >= global_type_registry->count) return NULL;
    
    NvTypeObject* type = global_type_registry->types[index];
    if (!type) return NULL;
    
    return (TypeInfo*)type->tp_cache;  // TypeInfo guardado em tp_cache
}

// Obter informações de tipo por nome
TypeInfo* get_type_info_by_name(const char* name) {
    if (!global_type_registry || !name) return NULL;
    
    for (int i = 0; i < global_type_registry->count; i++) {
        NvTypeObject* type = global_type_registry->types[i];
        if (type && type->tp_name && strcmp(type->tp_name, name) == 0) {
            return (TypeInfo*)type->tp_cache;
        }
    }
    
    return NULL;
}

// Verificar se tipo é válido
int is_valid_type(int32_t type) {
    return (type >= NV_INT_BASE && type <= NV_ANY_BASE) || type == NV_CHAR_BASE;
}

// Obter nome do tipo como string
const char* get_type_name(int32_t type) {
    switch (type) {
        case NV_INT_BASE: return "int";
        case NV_FLOAT_BASE: return "float";
        case NV_BOOL_BASE: return "bool";
        case NV_CHAR_BASE: return "char";
        case NV_STR_BASE: return "str";
        case NV_ARRAY_BASE: return "array";
        case NV_VECTOR_BASE: return "vector";
        case NV_MAP_BASE: return "map";
        case NV_TUPLE_BASE: return "tuple";
        case NV_TYPE_BASE: return "type";
        case NV_OBJECT_BASE: return "object";
        case NV_ANY_BASE: return "any";
        default: return "unknown";
    }
}
