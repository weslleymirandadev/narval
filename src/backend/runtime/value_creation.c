#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================= */
/*                    FUNÇÕES DE ACESSO E MANIPULAÇÃO        */
/* ============================================================= */

// Incrementar contador de referências (compatibilidade)
static inline void nv_incref(NvObject* obj) {
    if (obj) {
        obj->ref_count++;
    }
}

// Decrementar contador de referências (compatibilidade)
static inline void nv_decref(NvObject* obj) {
    if (obj && --obj->ref_count == 0) {
        // Liberar objeto
        if (obj->ob_type && obj->ob_type->tp_dealloc) {
            obj->ob_type->tp_dealloc(obj);
        } else {
            free(obj);
        }
    }
}

// Obter tipo de um valor (para compatibilidade com legado)
int32_t get_value_type(const Value* v) {
    if (!v || !v->obj) return 0;
    
    NvTypeObject* type = v->obj->ob_type;
    if (!type) return 0;
    
    // Mapear tipos novos para constantes legadas
    if (type == NVInt_Type) return NV_INT_BASE;
    if (type == NVFloat_Type) return NV_FLOAT_BASE;
    if (type == NVBool_Type) return NV_BOOL_BASE;
    if (type == NVStr_Type) return NV_STR_BASE;
    if (type == NVArray_Type) return NV_ARRAY_BASE;
    if (type == NVVector_Type) return NV_VECTOR_BASE;
    if (type == NVMap_Type) return NV_MAP_BASE;
    if (type == NVTuple_Type) return NV_TUPLE_BASE;
    if (type == NVObject_Type) return NV_OBJECT_BASE;
    if (type == NVType_Type) return NV_TYPE_BASE;
    
    return NV_ANY_BASE;
}

// Obter informações de tipo (para compatibilidade)
TypeInfo* get_value_type_info(const Value* v) {
    if (!v || !v->obj) return NULL;
    
    // No novo sistema, poderíamos implementar TypeInfo dinâmico
    // Por enquanto, retornar NULL para compatibilidade
    return NULL;
}

// Garantir tipo do valor (para compatibilidade)
void ensure_value_type(Value* v) {
    if (!v || !v->obj) return;
    
    // No novo sistema, o tipo já está garantido pela estrutura
    // Esta função é mantida para compatibilidade
}

// Validar tipo (para compatibilidade)
int validate_value_type(const Value* v, int32_t expected_type) {
    if (!v || !v->obj) return 0;
    
    int32_t actual_type = get_value_type(v);
    return actual_type == expected_type;
}

// Liberar valor (para compatibilidade)
void free_value(Value* v) {
    if (!v || !v->obj) return;
    
    // Usar sistema de referências do novo sistema
    nv_decref(v->obj);
    v->obj = NULL;
}
