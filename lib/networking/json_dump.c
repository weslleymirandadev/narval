#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Conjunto de ponteiros visitados para evitar ciclos */
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

static void json_string_escape(const char* str, FILE* out) {
    fputc('"', out);
    for (const char* p = str; *p; ++p) {
        switch (*p) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            case '\b': fputs("\\b", out); break;
            case '\f': fputs("\\f", out); break;
            default:
                if (*p >= 0 && *p < 32) {
                    fprintf(out, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, out);
                }
                break;
        }
    }
    fputc('"', out);
}

static void json_dump_value_recursive(const Value* v, FILE* out, int depth);

static void json_dump_array(const Array* a, FILE* out, int depth) {
    if (!a) {
        fputs("null", out);
        return;
    }

    uintptr_t ptr = (uintptr_t)a;
    if (is_visited(ptr)) {
        fputs("\"<array[cycle]>\"", out);
        return;
    }
    mark_visited(ptr);

    fputs("[", out);
    for (int i = 0; i < a->size; ++i) {
        if (i > 0) fputs(", ", out);
        json_dump_value_recursive(&a->elements[i], out, depth + 1);
    }
    fputs("]", out);
}

static void json_dump_vector(const Vector* vec, FILE* out, int depth) {
    if (!vec) {
        fputs("null", out);
        return;
    }

    uintptr_t ptr = (uintptr_t)vec;
    if (is_visited(ptr)) {
        fputs("\"<vector[cycle]>\"", out);
        return;
    }
    mark_visited(ptr);

    fputs("[", out);
    for (int i = 0; i < vec->size; ++i) {
        if (i > 0) fputs(", ", out);
        json_dump_value_recursive(&vec->elements[i], out, depth + 1);
    }
    fputs("]", out);
}

static void json_dump_map(const Map* m, FILE* out, int depth) {
    if (!m) {
        fputs("null", out);
        return;
    }

    uintptr_t ptr = (uintptr_t)m;
    if (is_visited(ptr)) {
        fputs("\"<map[cycle]>\"", out);
        return;
    }
    mark_visited(ptr);

    fputs("{", out);
    for (int i = 0; i < m->size; ++i) {
        if (!m->keys[i]) continue;
        if (i > 0) fputs(", ", out);
        
        json_string_escape(m->keys[i], out);
        fputs(": ", out);
        json_dump_value_recursive(&m->values[i], out, depth + 1);
    }
    fputs("}", out);
}

static void json_dump_tuple(const Tuple* t, FILE* out, int depth) {
    if (!t) {
        fputs("null", out);
        return;
    }

    uintptr_t ptr = (uintptr_t)t;
    if (is_visited(ptr)) {
        fputs("\"<tuple[cycle]>\"", out);
        return;
    }
    mark_visited(ptr);

    fputs("[", out);
    for (int i = 0; i < t->field_count; ++i) {
        if (i > 0) fputs(", ", out);
        json_dump_value_recursive(&t->fields[i], out, depth + 1);
    }
    fputs("]", out);
}

static void json_dump_value_recursive(const Value* v, FILE* out, int depth) {
    if (!v) {
        fputs("null", out);
        return;
    }

    // Limitar profundidade para evitar recursão excessiva
    if (depth > 10) {
        fputs("\"...[depth limit]...\"", out);
        return;
    }

    switch (v->type) {
        case TAG_INT: {
            fprintf(out, "%ld", v->value);
            break;
        }

        case TAG_FLOAT: {
            double d;
            memcpy(&d, &v->value, sizeof(double));
            fprintf(out, "%.17g", d);
            break;
        }

        case TAG_BOOL:
            fputs(v->value ? "true" : "false", out);
            break;

        case TAG_STR: {
            char* s = (char*)(intptr_t)v->value;
            if (s) {
                json_string_escape(s, out);
            } else {
                fputs("null", out);
            }
            break;
        }

        case TAG_ARRAY: {
            Array* a = (Array*)(intptr_t)v->value;
            json_dump_array(a, out, depth);
            break;
        }

        case TAG_VECTOR: {
            Vector* vec = (Vector*)(intptr_t)v->value;
            json_dump_vector(vec, out, depth);
            break;
        }

        case TAG_MAP: {
            Map* m = (Map*)(intptr_t)v->value;
            json_dump_map(m, out, depth);
            break;
        }

        case TAG_TUPLE: {
            Tuple* t = (Tuple*)(intptr_t)v->value;
            json_dump_tuple(t, out, depth);
            break;
        }

        case TAG_CUSTOM: {
            // Para tipos customizados, tentar usar TypeInfo se disponível
            TypeInfo* info = get_value_type_info(v);
            if (info && info->field_names && info->field_count > 0) {
                // Tratar como objeto JSON
                Value* fields = (Value*)(intptr_t)v->value;
                fputs("{", out);
                for (int i = 0; i < info->field_count; ++i) {
                    if (i > 0) fputs(", ", out);
                    json_string_escape(info->field_names[i], out);
                    fputs(": ", out);
                    json_dump_value_recursive(&fields[i], out, depth + 1);
                }
                fputs("}", out);
            } else {
                // Fallback: mostrar como string
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "\"<custom:%s>\"", 
                         info ? info->type_name : "unknown");
                fputs(buffer, out);
            }
            break;
        }

        default:
            // Valor nulo ou desconhecido
            fputs("null", out);
            break;
    }
}

// Função principal de serialização JSON
void json_dump(const Value* v, const char* filename) {
    if (!v || !filename) {
        return;
    }

    // Resetar visited pointers
    visited_count = 0;

    // Abrir arquivo para escrita
    FILE* f = fopen(filename, "wb");
    if (!f) {
        return;
    }

    // Serializar o valor
    json_dump_value_recursive(v, f, 0);
    
    fclose(f);
}
