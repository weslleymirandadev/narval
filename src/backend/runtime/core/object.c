#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Forward-declare NVTensor layout for field access in nv_object_get_field.
// The real definition lives in tensor/tensor.c.
typedef struct {
    NvObject_HEAD;
    int32_t  dtype;
    int32_t  ndim;
    int64_t* shape;
    int64_t* strides;
    void*    data;
    int64_t  nelem;
} NVTensor;

// Definição real de NVType_Type para resolver undefined references
NvTypeObject* NVType_Type = NULL;

#define ANSI_BOLD  "\x1b[1m"
#define ANSI_RESET "\x1b[0m"
#define ANSI_RED   "\x1b[31m"
#define ANSI_CYAN  "\x1b[36m"
#define ANSI_DIM   "\x1b[2m"

/* Shadow call stack */
NvTraceFrame nv_trace_stack[NV_TRACE_MAX_FRAMES];
int nv_trace_depth = 0;

void nv_push_frame(const char* file, const char* func) {
    if (nv_trace_depth < NV_TRACE_MAX_FRAMES) {
        nv_trace_stack[nv_trace_depth].file = file;
        nv_trace_stack[nv_trace_depth].func = func;
        nv_trace_stack[nv_trace_depth].line = 0;
        nv_trace_depth++;
    }
}

void nv_pop_frame(void) {
    if (nv_trace_depth > 0) nv_trace_depth--;
}

void nv_set_line(int line) {
    if (nv_trace_depth > 0)
        nv_trace_stack[nv_trace_depth - 1].line = line;
}

static void print_source_line(const char* file, int target_line) {
    FILE* f = fopen(file, "r");
    if (!f) return;
    char buf[1024];
    int cur = 1;
    while (fgets(buf, sizeof(buf), f)) {
        if (cur == target_line) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
                buf[--len] = '\0';
            char lno[16];
            snprintf(lno, sizeof(lno), "%d", target_line);
            fprintf(stderr, ANSI_BOLD " %s |" ANSI_RESET "   %s\n", lno, buf);
            fclose(f);
            return;
        }
        cur++;
    }
    fclose(f);
}

static void nv_print_runtime_error(const char* kind, const char* msg) {
    fprintf(stderr, "\n" ANSI_BOLD "Traceback (most recent call last):" ANSI_RESET "\n");
    for (int i = 0; i < nv_trace_depth; i++) {
        const char* file = nv_trace_stack[i].file ? nv_trace_stack[i].file : "<unknown>";
        const char* func = nv_trace_stack[i].func ? nv_trace_stack[i].func : "<unknown>";
        int line = nv_trace_stack[i].line;
        fprintf(stderr,
            ANSI_BOLD "  File " ANSI_RESET
            ANSI_CYAN "\"%s\"" ANSI_RESET
            ANSI_BOLD ", line %d, in %s" ANSI_RESET "\n",
            file, line, func);
        if (nv_trace_stack[i].file && line > 0)
            print_source_line(file, line);
    }
    fprintf(stderr, ANSI_BOLD ANSI_RED "%s" ANSI_RESET ANSI_BOLD ": %s" ANSI_RESET "\n", kind, msg);
    fflush(stderr);
    exit(1);
}

void nv_raise_value_error(const char* msg) {
    nv_print_runtime_error("ValueError", msg);
}

void nv_raise_type_error(const char* msg) {
    nv_print_runtime_error("TypeError", msg);
}

/* ============================================================= */
/*                    INICIALIZAÇÃO DO RUNTIME DO NARVAL        */
/* ============================================================= */

// Tipos builtin globais
NvTypeObject* NVInt_Type = NULL;
NvTypeObject* NVFloat_Type = NULL;
NvTypeObject* NVBool_Type = NULL;
NvTypeObject* NVChar_Type = NULL;
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

/* ── Deallocators for the builtin types ─────────────────────────────────────
 * nv_decref frees the OBJECT block when the count reaches zero, but the heap
 * payload the object owns — a string's buffer, an array's element vector, a map's
 * keys and values — was never released, so dropping a string freed 32 bytes and
 * leaked its bytes. A container also owns the elements stored in it: the drop pass
 * deliberately does not free a value whose last use is `push`/`set_`, so those are
 * released here, once, with the container. Cycles (an object reachable from
 * itself) simply keep the count above zero and leak, they never recurse: the
 * decref of a cycle stops at the first object it cannot free.
 */

static void nv_str_dealloc(NvObject* obj) {
    if (!obj) return;
    NVStr* s = (NVStr*)obj;
    free(s->value);
    free(s);
}

static void nv_sequence_dealloc(NvObject* obj) {
    if (!obj) return;
    NVArray* a = (NVArray*)obj;
    if (a->elements) {
        for (int i = 0; i < a->size; ++i) nv_decref(a->elements[i].obj);
        free(a->elements);
    }
    free(a);
}

static void nv_tuple_dealloc(NvObject* obj) {
    if (!obj) return;
    NVTuple* t = (NVTuple*)obj;
    if (t->fields) {
        for (int i = 0; i < t->field_count; ++i) nv_decref(t->fields[i].obj);
        free(t->fields);
    }
    free(t);
}

static void nv_map_dealloc(NvObject* obj) {
    if (!obj) return;
    NVMap* m = (NVMap*)obj;
    if (m->keys) {
        for (int i = 0; i < m->size; ++i) free(m->keys[i]);
        free(m->keys);
    }
    if (m->values) {
        for (int i = 0; i < m->size; ++i) nv_decref(m->values[i].obj);
        free(m->values);
    }
    free(m);
}

// Called after the builtin types exist (initialize_builtin_classes).
void nv_install_builtin_deallocs(void) {
    if (NVStr_Type)    NVStr_Type->tp_dealloc    = nv_str_dealloc;
    if (NVArray_Type)  NVArray_Type->tp_dealloc  = nv_sequence_dealloc;
    if (NVVector_Type) NVVector_Type->tp_dealloc = nv_sequence_dealloc;
    if (NVTuple_Type)  NVTuple_Type->tp_dealloc  = nv_tuple_dealloc;
    if (NVMap_Type)    NVMap_Type->tp_dealloc    = nv_map_dealloc;
}

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

    /* Caso especial: objetos Error têm `message` como campo C raw (não map-backed) */
    if (strcmp(key, "message") == 0) {
        NvTypeObject* t = self->obj->ob_type;
        int is_error = 0;
        if (t && t->tp_name) {
            size_t len = strlen(t->tp_name);
            is_error = (len >= 5 && strcmp(t->tp_name + len - 5, "Error") == 0);
        }
        if (is_error) {
            NVError* err_obj = (NVError*)self->obj;
            create_str(out, err_obj->message ? err_obj->message : "");
            return;
        }
    }

    /* Tensor field access (attributes like .shape, .ndim, .dtype) */
    if (self->obj->ob_type == NVTensor_Type) {
        NVTensor* t = (NVTensor*)self->obj;
        if (strcmp(key, "ndim") == 0) {
            create_int(out, t->ndim);
            return;
        }
        if (strcmp(key, "shape") == 0) {
            create_array(out, t->ndim);
            if (out->obj) {
                NVArray* arr = (NVArray*)out->obj;
                for (int i = 0; i < t->ndim; i++) {
                    Value v = {NULL};
                    create_int(&v, (int32_t)t->shape[i]);
                    arr->elements[i] = v;
                }
            }
            return;
        }
        if (strcmp(key, "dtype") == 0) {
            create_str(out, nv_tensor_dtype_name(t->dtype));
            return;
        }
        if (strcmp(key, "size") == 0 || strcmp(key, "nelem") == 0) {
            create_int(out, (int32_t)t->nelem);
            return;
        }
        if (strcmp(key, "T") == 0) {
            *out = nv_tensor_transpose(self);
            return;
        }
    }

    NVMap* map = (NVMap*)self->obj;
    for (int i = 0; i < map->size; i++) {
        if (map->keys[i] && strcmp(map->keys[i], key) == 0) {
            *out = map->values[i];
            return;
        }
    }
    out->obj = NULL;
}

void nv_set_str_method(Value* self, void* fn) {
    if (!self || !self->obj) return;
    if (self->obj->ob_type != NVMap_Type) return;
    ((NVMap*)self->obj)->str_method = (Value (*)(Value*))fn;
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
