#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Definição para resolver undefined reference se não for definida em nv_runtime.c
#ifndef NV_TYPE_TYPE_DEFINED
#define NV_TYPE_TYPE_DEFINED
extern NvTypeObject* NVType_Type;
#endif

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
    printf("DEBUG: nv_write() called - ENTRY\n");
    printf("DEBUG: v pointer = %p\n", v);
    
    if (!v) {
        printf("DEBUG: v is null - returning\n");
        printf("null");
        return;
    }
    
    // Verificar se o ponteiro é válido antes de acessar
    if ((uintptr_t)v < 0x1000) {
        printf("DEBUG: v pointer looks invalid (too small) - returning\n");
        printf("null");
        return;
    }
    
    printf("DEBUG: About to access v->obj\n");
    
    // DEBUG: Verificar o conteúdo bruto da estrutura Value
    printf("DEBUG: Raw Value struct content:\n");
    unsigned char* bytes = (unsigned char*)v;
    for (int i = 0; i < sizeof(Value); i++) {
        printf("%02x ", bytes[i]);
        if ((i + 1) % 8 == 0) printf("\n");
    }
    printf("\n");
    
    if (!v->obj) {
        printf("DEBUG: v->obj is null - returning\n");
        printf("null");
        return;
    }
    
    printf("DEBUG: v->obj = %p\n", v->obj);
    
    // Verificar se o ponteiro do objeto é válido
    if ((uintptr_t)v->obj < 0x1000) {
        printf("DEBUG: v->obj pointer looks invalid (too small) - returning\n");
        printf("null");
        return;
    }
    
    printf("DEBUG: About to access v->obj->ob_type\n");
    
    NvTypeObject* type = v->obj->ob_type;
    printf("DEBUG: type pointer = %p\n", type);
    
    if (!type) {
        printf("DEBUG: type is null - returning\n");
        printf("<unknown>");
        return;
    }
    
    printf("DEBUG: type->tp_name = %s\n", type->tp_name ? type->tp_name : "NULL");
    printf("DEBUG: NVStr_Type = %p\n", NVStr_Type);
    printf("DEBUG: NVInt_Type = %p\n", NVInt_Type);
    printf("DEBUG: NVFloat_Type = %p\n", NVFloat_Type);
    printf("DEBUG: About to compare type with NVStr_Type\n");
    
    // Verificar se os ponteiros são válidos antes de comparar
    if (type == NVStr_Type) {
        printf("DEBUG: Found string type\n");
        NVStr* str_obj = (NVStr*)v->obj;
        printf("DEBUG: str_obj = %p\n", str_obj);
        if (str_obj && str_obj->value) {
            printf("DEBUG: str_obj->value = %s\n", str_obj->value);
            printf("%s\n", str_obj->value);
        } else {
            printf("DEBUG: str_obj or str_obj->value is null\n");
            printf("\n");
        }
    } else if (type == NVInt_Type) {
        printf("DEBUG: Found int type\n");
        NVInt* int_obj = (NVInt*)v->obj;
        printf("%d\n", int_obj->value);
    } else if (type == NVFloat_Type) {
        printf("DEBUG: Found float type\n");
        NVFloat* float_obj = (NVFloat*)v->obj;
        printf("%f\n", float_obj->value);
    } else if (type == NVBool_Type) {
        printf("DEBUG: Found bool type\n");
        NVBool* bool_obj = (NVBool*)v->obj;
        printf("%s\n", bool_obj->value ? "true" : "false");
    } else {
        printf("DEBUG: Unknown type, falling back to <unknown>\n");
        printf("<%s>\n", type->tp_name ? type->tp_name : "object");
    }

    printf("DEBUG: nv_write() - EXIT\n");
    fflush(stdout);
}

// nv_write_no_nl: igual a nv_write mas sem '\n' (usado como prompt em read())
void nv_write_no_nl(Value* v) {
    if (!v || !v->obj) { printf("null"); return; }
    NvTypeObject* type = v->obj->ob_type;
    if (!type) { printf("<unknown>"); return; }
    if (type == NVStr_Type) {
        NVStr* s = (NVStr*)v->obj;
        if (s && s->value) printf("%s", s->value);
    } else if (type == NVInt_Type) {
        printf("%d", ((NVInt*)v->obj)->value);
    } else if (type == NVFloat_Type) {
        printf("%f", ((NVFloat*)v->obj)->value);
    } else if (type == NVBool_Type) {
        printf("%s", ((NVBool*)v->obj)->value ? "true" : "false");
    } else {
        printf("<%s>", type->tp_name ? type->tp_name : "object");
    }
    fflush(stdout);
}
