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
void vector_push_method(Value* out, Value* self_vec, Value* elem) {
    (void)out;
    if (!self_vec || !self_vec->obj || !elem) return;

    NVVector* v = (NVVector*)self_vec->obj;

    if (v->size >= v->capacity) {
        int new_capacity = v->capacity == 0 ? 4 : v->capacity * 2;
        Value* new_elements = (Value*)realloc(v->elements, new_capacity * sizeof(Value));
        if (!new_elements) return;
        v->elements = new_elements;
        v->capacity = new_capacity;
    }

    v->elements[v->size] = *elem;
    v->size++;
}

void vector_pop_method(Value* out, Value* self_vec) {
    if (out) memset(out, 0, sizeof(Value));
    if (!self_vec || !self_vec->obj) return;

    NVVector* v = (NVVector*)self_vec->obj;
    if (v->size == 0) return;

    v->size--;
    if (out) *out = v->elements[v->size];
}

void vector_get_method(Value* out, Value* self_vec, int32_t index) {
    if (out) memset(out, 0, sizeof(Value));
    if (!self_vec || !self_vec->obj) return;

    NVVector* v = (NVVector*)self_vec->obj;
    if (index < 0) index += v->size;
    if (index < 0 || index >= v->size) return;

    if (out) *out = v->elements[index];
}

void vector_set_method(Value* self_vec, int32_t index, Value* elem) {
    if (!self_vec || !self_vec->obj || !elem) return;

    NVVector* v = (NVVector*)self_vec->obj;
    if (index < 0) index += v->size;
    if (index < 0 || index >= v->size) return;

    v->elements[index] = *elem;
}

// Indexing helpers used by codegen
void array_get_index_v(Value* out, Value* self_arr, int32_t index) {
    if (out) memset(out, 0, sizeof(Value));
    if (!self_arr || !self_arr->obj) return;

    NVArray* a = (NVArray*)self_arr->obj;
    if (index < 0) index += a->size;
    if (index < 0 || index >= a->size) return;

    if (out) *out = a->elements[index];
}

void array_set_index_v(Value* self_arr, int32_t index, Value* elem) {
    if (!self_arr || !self_arr->obj || !elem) return;

    NVArray* a = (NVArray*)self_arr->obj;
    if (index < 0) index += a->size;
    if (index < 0 || index >= a->size) return;

    a->elements[index] = *elem;
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

int32_t nv_get_iterable_length(Value* self) {
    if (!self || !self->obj) return 0;
    NvObject* obj = self->obj;
    if (obj->ob_type == NVVector_Type || obj->ob_type == NVArray_Type) {
        return ((NVArray*)obj)->size;
    }
    if (obj->ob_type == NVStr_Type) {
        return (int32_t)((NVStr*)obj)->len;
    }
    return 0;
}

/* ============================================================= */
/*                    SLICE DE COLEÇÕES                          */
/* ============================================================= */

/* Sentinela para "não especificado": INT32_MIN */
#define NV_SLICE_NONE (-2147483648)

static void slice_push(NVVector* v, Value elem) {
    if (v->size >= v->capacity) {
        int new_cap = v->capacity == 0 ? 4 : v->capacity * 2;
        v->elements = (Value*)realloc(v->elements, new_cap * sizeof(Value));
        v->capacity = new_cap;
    }
    v->elements[v->size++] = elem;
}

void nv_collection_slice(Value* out, Value* self, int32_t start, int32_t stop, int32_t step) {
    if (out) memset(out, 0, sizeof(Value));
    if (!self || !self->obj) { create_vector(out, 0); return; }

    NvObject* obj = self->obj;
    Value* elems = NULL;
    int size = 0;

    if (obj->ob_type == NVVector_Type) {
        NVVector* v = (NVVector*)obj;
        elems = v->elements;
        size  = v->size;
    } else if (obj->ob_type == NVArray_Type) {
        NVArray* a = (NVArray*)obj;
        elems = a->elements;
        size  = a->size;
    } else {
        create_vector(out, 0);
        return;
    }

    /* Normalizar step */
    if (step == NV_SLICE_NONE) step = 1;
    if (step == 0) { create_vector(out, 0); return; }

    /* Normalizar start */
    if (start == NV_SLICE_NONE) {
        start = (step > 0) ? 0 : size - 1;
    } else {
        if (start < 0) start += size;
        if (step > 0) {
            if (start < 0) start = 0;
            if (start > size) start = size;
        } else {
            if (start < 0) start = -1;
            if (start >= size) start = size - 1;
        }
    }

    /* Normalizar stop */
    if (stop == NV_SLICE_NONE) {
        stop = (step > 0) ? size : -1;
    } else {
        if (stop < 0) stop += size;
        if (step > 0) {
            if (stop < 0) stop = 0;
            if (stop > size) stop = size;
        } else {
            if (stop < 0) stop = -1;
            if (stop >= size) stop = size - 1;
        }
    }

    create_vector(out, 4);
    NVVector* result = (NVVector*)out->obj;

    if (step > 0) {
        for (int i = start; i < stop; i += step)
            slice_push(result, elems[i]);
    } else {
        for (int i = start; i > stop; i += step)
            slice_push(result, elems[i]);
    }
}

/* ============================================================= */
/*                    FUNÇÕES DE I/O                             */
/* ============================================================= */

// Função de escrita principal
void nv_write(Value* v) {
    if (!v) {
        printf("null\n");
        return;
    }

    if ((uintptr_t)v < 0x1000) {
        printf("null\n");
        return;
    }
    
    // Tentar acessar v->obj com verificação adicional
    NvObject* obj = v->obj;
    
    if (!obj) {
        printf("null\n");
        return;
    }
    
    // Verificar se ob_type é válido
    if ((uintptr_t)obj < 0x1000) {
        printf("null");
        return;
    }
    
    NvTypeObject* type = obj->ob_type;
    
    if (!type) {
        printf("<unknown>");
        return;
    }
    
    // Verificar se type é válido
    if ((uintptr_t)type < 0x1000) {
        printf("<unknown>");
        return;
    }
        
    // Verificar se os ponteiros são válidos antes de comparar
    if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)obj;
        if (str_obj && str_obj->value) {
            printf("%s\n", str_obj->value);
        } else {
            printf("\n");
        }
    } else if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)obj;
        printf("%d\n", int_obj->value);
    } else if (type == NVFloat_Type) {
        NVFloat* float_obj = (NVFloat*)obj;
        printf("%f\n", float_obj->value);
    } else if (type == NVBool_Type) {
        NVBool* bool_obj = (NVBool*)obj;
        printf("%s\n", bool_obj->value ? "true" : "false");
    } else {
        printf("<%s>\n", type->tp_name ? type->tp_name : "object");
    }

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

char* nv_read(const char* prompt) {
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }

    size_t bufsize = 128;
    char* buf = (char*)malloc(bufsize);
    if (!buf) return NULL;

    size_t idx = 0;
    int ch;
    while ((ch = getchar()) != EOF && ch != '\n') {
        if (idx + 1 >= bufsize) {
            size_t new_size = bufsize * 2;
            char* n = (char*)realloc(buf, new_size);
            if (!n) {
                free(buf);
                return NULL;
            }
            buf = n;
            bufsize = new_size;
        }
        buf[idx++] = (char)ch;
    }

    if (ch == EOF && idx == 0) {
        free(buf);
        return NULL;
    }

    buf[idx] = '\0';
    return buf;
}