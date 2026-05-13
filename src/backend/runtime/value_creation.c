#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Declaração da função de inicialização
extern void register_global_init(void);

/* ============================================================= */
/*                    FUNÇÕES DE ACESSO E MANIPULAÇÃO        */
/* ============================================================= */


// Obter tipo de um valor (para compatibilidade com legado)
int32_t get_value_type(const Value* v) {
    if (!v || !v->obj) return 0;
    
    NvTypeObject* type = v->obj->ob_type;
    if (!type) return 0;
    
    // Mapear tipos novos para constantes legadas
    if (type == NVInt_Type) return NV_INT_BASE;
    if (type == NVFloat_Type) return NV_FLOAT_BASE;
    if (type == NVBool_Type) return NV_BOOL_BASE;
    if (type == NVChar_Type) return NV_CHAR_BASE;
    if (type == NVStr_Type) return NV_STR_BASE;
    if (type == NVArray_Type) return NV_ARRAY_BASE;
    if (type == NVVector_Type) return NV_VECTOR_BASE;
    if (type == NVMap_Type) return NV_MAP_BASE;
    if (type == NVTuple_Type) return NV_TUPLE_BASE;
    if (type == NVObject_Type) return NV_OBJECT_BASE;
    if (type == NVType_Type) return NV_TYPE_BASE;
    if (type == NVOptionNone_Type) return NV_OPTION_NONE_BASE;
    if (type == NVOptionSome_Type) return NV_OPTION_SOME_BASE;
    if (type == NVResultOk_Type)   return NV_RESULT_OK_BASE;
    if (type == NVResultErr_Type)  return NV_RESULT_ERR_BASE;

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
    if (!v) {
        return;
    }
    
    if (!v->obj) {
        return;
    }
    
    if (!v->obj->ob_type) {
        
        // Tentar determinar o tipo pelo conteúdo do objeto
        // Se o objeto parece ser uma string, configurar o tipo correto
        // Isso é um fallback para objetos que foram criados sem o tipo correto
        
        // Por enquanto, apenas retornar para evitar crash
        // TODO: Implementar detecção de tipo baseada no conteúdo
        return;
    }
    
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

/* ============================================================= */
/*                    FUNÇÕES DE CRIAÇÃO USANDO CLASSES          */
/* ============================================================= */

void create_int(Value* out, int32_t value) {
    if (!out) return;
    
    // Limpar struct para evitar problemas
    memset(out, 0, sizeof(Value));
    
    // Se os tipos não estiverem inicializados, inicializá-los primeiro
    if (!NVInt_Type) {
        register_global_init();
    }
    
    // Se ainda não estiverem inicializados, criar objeto básico
    if (!NVInt_Type) {
        // Criar NVInt básico sem tipo por enquanto (fallback)
        NVInt* int_obj = (NVInt*)calloc(1, sizeof(NVInt));
        if (!int_obj) {
            out->obj = NULL;
            return;
        }
        
        int_obj->ob_base.ob_type = NULL;
        int_obj->ob_base.ref_count = 1;
        int_obj->ob_base.flags = 0;
        int_obj->value = value;
        
        out->obj = (NvObject*)int_obj;
        return;
    }
    
    NVInt* int_obj = (NVInt*)calloc(1, sizeof(NVInt));
    if (!int_obj) {
        out->obj = NULL;
        return;
    }
    int_obj->ob_base.ob_type = NVInt_Type;
    int_obj->ob_base.ref_count = 1;
    int_obj->ob_base.flags = 0;
    int_obj->value = value;
    out->obj = (NvObject*)int_obj;
}

void create_float(Value* out, double value) {
    if (!out) return;
    
    memset(out, 0, sizeof(Value));
    
    // Se os tipos não estiverem inicializados, inicializá-los primeiro
    if (!NVFloat_Type) {
        register_global_init();
    }
    
    if (!NVFloat_Type) {
        // Fallback
        NVFloat* float_obj = (NVFloat*)calloc(1, sizeof(NVFloat));
        if (!float_obj) {
            out->obj = NULL;
            return;
        }
        
        float_obj->ob_base.ob_type = NULL;
        float_obj->ob_base.ref_count = 1;
        float_obj->ob_base.flags = 0;
        float_obj->value = value;
        
        out->obj = (NvObject*)float_obj;
        return;
    }
    
    // Alocar diretamente com tamanho correto (nv_object_new usa tp_basicsize que pode ser
    // inicializado como sizeof(NvObject) antes do override em create_builtin_numeric_class)
    NVFloat* float_obj = (NVFloat*)calloc(1, sizeof(NVFloat));
    if (!float_obj) {
        out->obj = NULL;
        return;
    }
    float_obj->ob_base.ob_type = NVFloat_Type;
    float_obj->ob_base.ref_count = 1;
    float_obj->ob_base.flags = 0;
    float_obj->value = value;
    out->obj = (NvObject*)float_obj;
}

void create_bool(Value* out, int32_t value) {
    if (!out) return;
    
    memset(out, 0, sizeof(Value));
    
    if (!NVBool_Type) {
        // Fallback
        NVBool* bool_obj = (NVBool*)calloc(1, sizeof(NVBool));
        if (!bool_obj) {
            out->obj = NULL;
            return;
        }
        
        bool_obj->ob_base.ob_type = NULL;
        bool_obj->ob_base.ref_count = 1;
        bool_obj->ob_base.flags = 0;
        bool_obj->value = value ? 1 : 0;
        
        out->obj = (NvObject*)bool_obj;
        return;
    }
    
    // Criar NVBool usando o novo sistema
    NVBool* bool_obj = (NVBool*)nv_object_new(NVBool_Type, NULL, NULL);
    if (!bool_obj) {
        out->obj = NULL;
        return;
    }
    
    bool_obj->value = value ? 1 : 0;
    out->obj = (NvObject*)bool_obj;
}

void create_char(Value* out, char value) {
    if (!out) return;

    memset(out, 0, sizeof(Value));

    if (!NVChar_Type) {
        register_global_init();
    }

    if (!NVChar_Type) {
        NVChar* char_obj = (NVChar*)calloc(1, sizeof(NVChar));
        if (!char_obj) {
            out->obj = NULL;
            return;
        }

        char_obj->ob_base.ob_type = NULL;
        char_obj->ob_base.ref_count = 1;
        char_obj->ob_base.flags = 0;
        char_obj->value = value;

        out->obj = (NvObject*)char_obj;
        return;
    }

    NVChar* char_obj = (NVChar*)calloc(1, sizeof(NVChar));
    if (!char_obj) {
        out->obj = NULL;
        return;
    }

    char_obj->ob_base.ob_type = NVChar_Type;
    char_obj->ob_base.ref_count = 1;
    char_obj->ob_base.flags = 0;
    char_obj->value = value;
    out->obj = (NvObject*)char_obj;
}

void create_str(Value* out, const char* value) {
    if (!out) return;
    
    memset(out, 0, sizeof(Value));
    
    // Se os tipos não estiverem inicializados, inicializá-los primeiro
    if (!NVStr_Type) {
        register_global_init();
    }
    
    if (!NVStr_Type) {
        // Fallback
        NVStr* str_obj = (NVStr*)calloc(1, sizeof(NVStr));
        if (!str_obj) {
            out->obj = NULL;
            return;
        }
        
        str_obj->ob_base.ob_type = NULL;
        str_obj->ob_base.ref_count = 1;
        str_obj->ob_base.flags = 0;
        str_obj->value = value ? strdup(value) : NULL;
        str_obj->len = value ? strlen(value) : 0;
        
        out->obj = (NvObject*)str_obj;
        return;
    }
    
    // Criar NVStr usando o novo sistema
    NVStr* str_obj = (NVStr*)nv_object_new(NVStr_Type, NULL, NULL);
    if (!str_obj) {
        out->obj = NULL;
        return;
    }
    
    str_obj->value = value ? strdup(value) : NULL;
    str_obj->len = value ? strlen(value) : 0;
    out->obj = (NvObject*)str_obj;
}

void create_array(Value* out, int32_t size) {
    if (!out) return;

    memset(out, 0, sizeof(Value));

    if (!NVArray_Type) {
        NVArray* array_obj = (NVArray*)calloc(1, sizeof(NVArray));
        if (!array_obj) {
            out->obj = NULL;
            return;
        }

        array_obj->ob_base.ob_type = NULL;
        array_obj->ob_base.ref_count = 1;
        array_obj->ob_base.flags = 0;
        array_obj->size = 0;
        array_obj->capacity = 0;

        if (size > 0) {
            array_obj->elements = (Value*)calloc(size, sizeof(Value));
            if (array_obj->elements) { array_obj->capacity = size; array_obj->size = size; }
        } else {
            array_obj->elements = NULL;
        }

        out->obj = (NvObject*)array_obj;
        return;
    }

    NVArray* array_obj = (NVArray*)nv_object_new(NVArray_Type, NULL, NULL);
    if (!array_obj) {
        out->obj = NULL;
        return;
    }

    array_obj->size = 0;
    array_obj->capacity = 0;
    if (size > 0) {
        array_obj->elements = (Value*)calloc(size, sizeof(Value));
        if (array_obj->elements) { array_obj->capacity = size; array_obj->size = size; }
    } else {
        array_obj->elements = NULL;
    }
    out->obj = (NvObject*)array_obj;
}

void create_vector(Value* out, int32_t capacity) {
    if (!out) return;

    memset(out, 0, sizeof(Value));

    if (!NVVector_Type) {
        NVVector* vector_obj = (NVVector*)calloc(1, sizeof(NVVector));
        if (!vector_obj) {
            out->obj = NULL;
            return;
        }

        vector_obj->ob_base.ob_type = NULL;
        vector_obj->ob_base.ref_count = 1;
        vector_obj->ob_base.flags = 0;
        vector_obj->elements = NULL;
        vector_obj->size = 0;
        vector_obj->capacity = 0;

        if (capacity > 0) {
            vector_obj->elements = (Value*)malloc(capacity * sizeof(Value));
            if (vector_obj->elements) vector_obj->capacity = capacity;
        }

        out->obj = (NvObject*)vector_obj;
        return;
    }

    NVVector* vector_obj = (NVVector*)nv_object_new(NVVector_Type, NULL, NULL);
    if (!vector_obj) {
        out->obj = NULL;
        return;
    }

    vector_obj->size = 0;
    if (capacity > 0) {
        vector_obj->elements = (Value*)malloc(capacity * sizeof(Value));
        vector_obj->capacity = vector_obj->elements ? capacity : 0;
    } else {
        vector_obj->elements = NULL;
        vector_obj->capacity = 0;
    }
    out->obj = (NvObject*)vector_obj;
}

void create_map(Value* out) {
    if (!out) return;
    
    memset(out, 0, sizeof(Value));
    
    if (!NVMap_Type) {
        // Fallback
        NVMap* map_obj = (NVMap*)calloc(1, sizeof(NVMap));
        if (!map_obj) {
            out->obj = NULL;
            return;
        }
        
        map_obj->ob_base.ob_type = NULL;
        map_obj->ob_base.ref_count = 1;
        map_obj->ob_base.flags = 0;
        map_obj->keys = NULL;
        map_obj->values = NULL;
        map_obj->size = 0;
        map_obj->capacity = 0;
        
        out->obj = (NvObject*)map_obj;
        return;
    }
    
    // Criar NVMap usando o novo sistema
    NVMap* map_obj = (NVMap*)nv_object_new(NVMap_Type, NULL, NULL);
    if (!map_obj) {
        out->obj = NULL;
        return;
    }
    
    map_obj->keys = NULL;
    map_obj->values = NULL;
    map_obj->size = 0;
    map_obj->capacity = 0;
    out->obj = (NvObject*)map_obj;
}

void create_tuple(Value* out, int32_t field_count) {
    if (!out) return;

    memset(out, 0, sizeof(Value));

    if (!NVTuple_Type) {
        NVTuple* tuple_obj = (NVTuple*)calloc(1, sizeof(NVTuple));
        if (!tuple_obj) {
            out->obj = NULL;
            return;
        }

        tuple_obj->ob_base.ob_type = NULL;
        tuple_obj->ob_base.ref_count = 1;
        tuple_obj->ob_base.flags = 0;
        tuple_obj->field_count = 0;

        if (field_count > 0) {
            tuple_obj->fields = (Value*)calloc(field_count, sizeof(Value));
            if (tuple_obj->fields) tuple_obj->field_count = field_count;
        } else {
            tuple_obj->fields = NULL;
        }

        out->obj = (NvObject*)tuple_obj;
        return;
    }

    NVTuple* tuple_obj = (NVTuple*)nv_object_new(NVTuple_Type, NULL, NULL);
    if (!tuple_obj) {
        out->obj = NULL;
        return;
    }

    tuple_obj->field_count = 0;
    if (field_count > 0) {
        tuple_obj->fields = (Value*)calloc(field_count, sizeof(Value));
        if (tuple_obj->fields) tuple_obj->field_count = field_count;
    } else {
        tuple_obj->fields = NULL;
    }
    out->obj = (NvObject*)tuple_obj;
}

void create_any(Value* out) {
    // 'any' é um tipo dinâmico, por enquanto usa object
    if (!out) return;
    
    memset(out, 0, sizeof(Value));
    
    if (!NVObject_Type) {
        out->obj = NULL;
        return;
    }
    
    // Criar objeto genérico
    NvObject* obj = nv_object_new(NVObject_Type, NULL, NULL);
    out->obj = obj;
}

/* ─── Tuple element access ─── */

void tuple_set_impl(Value* self, int32_t index, Value* elem) {
    if (!self || !self->obj || !elem) return;
    NVTuple* t = (NVTuple*)self->obj;
    if (index < 0 || index >= t->field_count) return;
    t->fields[index] = *elem;
}

void tuple_get_impl(Value* out, Value* self, int32_t index) {
    if (!out) return;
    if (!self || !self->obj) { out->obj = NULL; return; }
    NVTuple* t = (NVTuple*)self->obj;
    if (index < 0 || index >= t->field_count) { out->obj = NULL; return; }
    *out = t->fields[index];
}
