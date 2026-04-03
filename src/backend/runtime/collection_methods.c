#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================= */
/*                    MÉTODOS DE COLEÇÕES - NOVO SISTEMA   */
/* ============================================================= */

// Métodos de Map
Value map_get_method(Map* m, const char* key) {
    Value result = {0};
    
    if (!m || !key) {
        result.obj = NULL;
        return result;
    }
    
    // Procurar chave no map
    for (int i = 0; i < m->size; i++) {
        if (m->keys[i] && strcmp(m->keys[i], key) == 0) {
            return m->values[i];
        }
    }
    
    // Não encontrou, criar valor any
    create_any(&result);
    return result;
}

void map_set_method(Map* m, const char* key, Value val) {
    if (!m || !key) return;
    
    // Procurar se chave já existe
    for (int i = 0; i < m->size; i++) {
        if (m->keys[i] && strcmp(m->keys[i], key) == 0) {
            m->values[i] = val;
            return;
        }
    }
    
    // Adicionar nova chave
    if (m->size >= m->capacity) {
        int new_capacity = m->capacity == 0 ? 4 : m->capacity * 2;
        m->keys = (char**)realloc(m->keys, new_capacity * sizeof(char*));
        m->values = (Value*)realloc(m->values, new_capacity * sizeof(Value));
        m->capacity = new_capacity;
    }
    
    m->keys[m->size] = strdup(key);
    m->values[m->size] = val;
    m->size++;
}

// Métodos de Vector
void vector_push_method(Vector* v, Value val) {
    if (!v) return;
    
    // Expandir capacidade se necessário
    if (v->size >= v->capacity) {
        int new_capacity = v->capacity == 0 ? 4 : v->capacity * 2;
        v->elements = (Value*)realloc(v->elements, new_capacity * sizeof(Value));
        v->capacity = new_capacity;
    }
    
    v->elements[v->size] = val;
    v->size++;
}

Value vector_pop_method(Vector* v) {
    Value result = {0};
    
    if (!v || v->size == 0) {
        result.obj = NULL;
        return result;
    }
    
    result = v->elements[v->size - 1];
    v->size--;
    
    return result;
}

Value vector_get_method(Vector* v, int index) {
    Value result = {0};
    
    if (!v || index < 0 || index >= v->size) {
        result.obj = NULL;
        return result;
    }
    
    return v->elements[index];
}

void vector_set_method(Vector* v, int index, Value val) {
    if (!v || index < 0 || index >= v->size) return;
    
    v->elements[index] = val;
}

// Métodos de Array (similar a Vector)
void array_push_method(Array* a, Value val) {
    if (!a) return;
    
    // Expandir capacidade se necessário
    if (a->size >= a->capacity) {
        int new_capacity = a->capacity == 0 ? 4 : a->capacity * 2;
        a->elements = (Value*)realloc(a->elements, new_capacity * sizeof(Value));
        a->capacity = new_capacity;
    }
    
    a->elements[a->size] = val;
    a->size++;
}

Value array_pop_method(Array* a) {
    Value result = {0};
    
    if (!a || a->size == 0) {
        result.obj = NULL;
        return result;
    }
    
    result = a->elements[a->size - 1];
    a->size--;
    
    return result;
}

/* ============================================================= */
/*                    FUNÇÕES DE I/O                             */
/* ============================================================= */

// Função de escrita principal
void nv_write(Value* v) {
    if (!v || !v->obj) {
        printf("null");
        return;
    }
    
    NvTypeObject* type = v->obj->ob_type;
    if (!type) {
        printf("<unknown>");
        return;
    }
    
    // Usar sistema de impressão baseado no tipo
    if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)v->obj;
        printf("%d", int_obj->value);
    } else if (type == NVFloat_Type) {
        NVFloat* float_obj = (NVFloat*)v->obj;
        printf("%f", float_obj->value);
    } else if (type == NVBool_Type) {
        NVBool* bool_obj = (NVBool*)v->obj;
        printf("%s", bool_obj->value ? "true" : "false");
    } else if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)v->obj;
        printf("%s", str_obj->value ? str_obj->value : "");
    } else if (type == NVArray_Type) {
        NVArray* array_obj = (NVArray*)v->obj;
        printf("[");
        for (int i = 0; i < array_obj->size; i++) {
            if (i > 0) printf(", ");
            nv_write(&array_obj->elements[i]);
        }
        printf("]");
    } else if (type == NVVector_Type) {
        NVVector* vector_obj = (NVVector*)v->obj;
        printf("[");
        for (int i = 0; i < vector_obj->size; i++) {
            if (i > 0) printf(", ");
            nv_write(&vector_obj->elements[i]);
        }
        printf("]");
    } else if (type == NVMap_Type) {
        NVMap* map_obj = (NVMap*)v->obj;
        printf("{");
        for (int i = 0; i < map_obj->size; i++) {
            if (i > 0) printf(", ");
            printf("\"%s\": ", map_obj->keys[i]);
            nv_write(&map_obj->values[i]);
        }
        printf("}");
    } else if (type == NVTuple_Type) {
        NVTuple* tuple_obj = (NVTuple*)v->obj;
        printf("(");
        for (int i = 0; i < tuple_obj->field_count; i++) {
            if (i > 0) printf(", ");
            nv_write(&tuple_obj->fields[i]);
        }
        printf(")");
    } else {
        // Tipo customizado ou desconhecido
        printf("<%s>", type->tp_name ? type->tp_name : "object");
    }

    fflush(stdout);
}
