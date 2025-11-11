#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "backend/runtime/rph_runtime.h"

/* Prototypes to help detect kind even if type tag is corrupted */
extern void* string_prototype;
extern void* array_prototype;
extern void* vector_prototype;
extern void* map_prototype;

/* ============================================================= */
/*                     PRINT RECURSIVO SEGURO                    */
/* ============================================================= */

/* Conjunto simples de ponteiros visitados (hash set básico) */
#define MAX_VISITED 256
static uintptr_t visited[MAX_VISITED];
static int visited_count = 0;

static int is_visited(uintptr_t ptr) {
    for (int i = 0; i < visited_count; ++i) {
        if (visited[i] == ptr) return 1;
    }
    return 0;
}

static void mark_visited(uintptr_t ptr) {
    if (visited_count < MAX_VISITED) {
        visited[visited_count++] = ptr;
    }
}

static void print_indent(int depth) {
    for (int i = 0; i < depth; ++i) fputs("  ", stdout);
}

static void rph_print_value_recursive(Value v, int depth);

/* Normalize type tag based on prototype, to guard against ABI/tag corruption */
static inline Value normalize_value(Value v) {
    if (v.prototype == string_prototype && v.type != TAG_STR) v.type = TAG_STR;
    if (v.prototype == array_prototype  && v.type != TAG_ARRAY) v.type = TAG_ARRAY;
    if (v.prototype == vector_prototype && v.type != TAG_VECTOR) v.type = TAG_VECTOR;
    if (v.prototype == map_prototype    && v.type != TAG_MAP) v.type = TAG_MAP;
    return v;
}

/* --- Imprime array --- */
static void print_array(Array* a, int depth) {
    if (!a) {
        fputs("null", stdout);
        return;
    }

    uintptr_t ptr = (uintptr_t)a;
    if (is_visited(ptr)) {
        fputs("<array[cycle]>", stdout);
        return;
    }
    mark_visited(ptr);

    fputs("{", stdout);
    for (int i = 0; i < a->size; ++i) {
        if (i > 0) fputs(", ", stdout);
        if (depth < 3) {  // limite de profundidade
            rph_print_value_recursive(a->elements[i], depth + 1);
        } else {
            fputs("...", stdout);
        }
    }
    fputs("}", stdout);
}

/* --- Imprime vector --- */
static void print_vector(Vector* vec, int depth) {
    if (!vec) {
        fputs("null", stdout);
        return;
    }

    uintptr_t ptr = (uintptr_t)vec;
    if (is_visited(ptr)) {
        fputs("<vector[cycle]>", stdout);
        return;
    }
    mark_visited(ptr);

    fputs("[", stdout);
    for (int i = 0; i < vec->size; ++i) {
        if (i > 0) fputs(", ", stdout);
        if (depth < 3) {
            rph_print_value_recursive(vec->elements[i], depth + 1);
        } else {
            fputs("...", stdout);
        }
    }
    fputs("]", stdout);
}

/* --- Imprime map --- */
static void print_map(Map* m, int depth) {
    if (!m) {
        fputs("null", stdout);
        return;
    }

    uintptr_t ptr = (uintptr_t)m;
    if (is_visited(ptr)) {
        fputs("<map[cycle]>", stdout);
        return;
    }
    mark_visited(ptr);

    fputs("{", stdout);
    int first = 1;
    for (int i = 0; i < m->size; ++i) {
        if (!m->keys[i]) continue;
        if (!first) fputs(", ", stdout);
        first = 0;

        char* key = m->keys[i];
        fputs("\"", stdout);
        fputs(key, stdout);
        fputs("\": ", stdout);

        if (depth < 3) {
            rph_print_value_recursive(m->values[i], depth + 1);
        } else {
            fputs("...", stdout);
        }
    }
    fputs("}", stdout);
}

/* --- Imprime tuple --- */
static void print_tuple(Tuple* t, int depth) {
    if (!t) {
        fputs("null", stdout);
        return;
    }

    uintptr_t ptr = (uintptr_t)t;
    if (is_visited(ptr)) {
        fputs("<tuple[cycle]>", stdout);
        return;
    }
    mark_visited(ptr);

    fputs("(", stdout);
    for (int i = 0; i < t->field_count; ++i) {
        if (i > 0) fputs(", ", stdout);
        if (depth < 3) {
            rph_print_value_recursive(t->fields[i], depth + 1);
        } else {
            fputs("...", stdout);
        }
    }
    fputs(")", stdout);
}

/* --- Função principal recursiva --- */
static void rph_print_value_recursive(Value v, int depth) {
    v = normalize_value(v);
    switch (v.type) {
        case TAG_INT: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", v.value);
            fputs(buf, stdout);
            break;
        }

        case TAG_FLOAT: {
            double d;
            memcpy(&d, &v.value, sizeof(double));
            char buf[64];
            gcvt(d, 17, buf);
            fputs(buf, stdout);
            break;
        }

        case TAG_BOOL:
            fputs(v.value ? "true" : "false", stdout);
            break;

        case TAG_STR: {
            char* s = (char*)(intptr_t)v.value;
            if (!s) {
                fputs("(null)", stdout);
            } else {
                if (depth == 0) {
                    /* Top-level string: print raw, no quotes */
                    fputs(s, stdout);
                } else {
                    /* Nested in container: print with quotes and escaping */
                    fputs("\"", stdout);
                    for (char* p = s; *p; ++p) {
                        if (*p == '"') fputs("\\\"", stdout);
                        else if (*p == '\n') fputs("\\n", stdout);
                        else if (*p == '\t') fputs("\\t", stdout);
                        else if (*p >= 32 && *p <= 126) fputc(*p, stdout);
                        else fputs("?", stdout);
                    }
                    fputs("\"", stdout);
                }
            }
            break;
        }

        case TAG_ARRAY: {
            Array* a = (Array*)(intptr_t)v.value;
            print_array(a, depth);
            break;
        }

        case TAG_VECTOR: {
            Vector* vec = (Vector*)(intptr_t)v.value;
            print_vector(vec, depth);
            break;
        }

        case TAG_MAP: {
            Map* m = (Map*)(intptr_t)v.value;
            print_map(m, depth);
            break;
        }

        case TAG_TUPLE: {
            Tuple* t = (Tuple*)(intptr_t)v.value;
            print_tuple(t, depth);
            break;
        }

        case TAG_ANY:
            fputs("<any>", stdout);
            break;

        default: {
            /* Last-chance: try prototype-based guess */
            if (v.prototype == string_prototype) {
                char* s = (char*)(intptr_t)v.value;
                if (!s) {
                    fputs("(null)", stdout);
                } else if (depth == 0) {
                    /* Top-level string: print raw, no quotes */
                    fputs(s, stdout);
                } else {
                    /* Nested in container: print with quotes and escaping */
                    fputs("\"", stdout);
                    for (char* p = s; *p; ++p) {
                        if (*p == '"') fputs("\\\"", stdout);
                        else if (*p == '\n') fputs("\\n", stdout);
                        else if (*p == '\t') fputs("\\t", stdout);
                        else if (*p >= 32 && *p <= 126) fputc(*p, stdout);
                        else fputs("?", stdout);
                    }
                    fputs("\"", stdout);
                }
            } else if (v.prototype == array_prototype) {
                print_array((Array*)(intptr_t)v.value, depth);
            } else if (v.prototype == vector_prototype) {
                print_vector((Vector*)(intptr_t)v.value, depth);
            } else if (v.prototype == map_prototype) {
                print_map((Map*)(intptr_t)v.value, depth);
            } else {
                fputs("<unknown>", stdout);
            }
            break;
        }
    }
}

/* ============================================================= */
/*                     API PÚBLICA                               */
/* ============================================================= */

__attribute__((force_align_arg_pointer))
void rph_write(Value* v) {
    visited_count = 0;  // reset a cada chamada
    if (v) {
        rph_print_value_recursive(*v, 0);
    } else {
        fputs("null", stdout);
    }
    fputs("\n", stdout);
    fflush(NULL); /* garante flush completo antes de _exit */
}

__attribute__((force_align_arg_pointer))
void rph_write_no_nl(Value* v) {
    visited_count = 0;  // reset a cada chamada
    if (v) {
        rph_print_value_recursive(*v, 0);
    } else {
        fputs("null", stdout);
    }
    fflush(NULL);
}