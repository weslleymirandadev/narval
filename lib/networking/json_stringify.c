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

static void json_stringify_value_recursive(const Value* v, FILE* out, int depth);

static void json_stringify_nv_array(const NVArray* a, FILE* out, int depth) {
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
        json_stringify_value_recursive(&a->elements[i], out, depth + 1);
    }
    fputs("]", out);
}

static void json_stringify_nv_vector(const NVVector* vec, FILE* out, int depth) {
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
        json_stringify_value_recursive(&vec->elements[i], out, depth + 1);
    }
    fputs("]", out);
}

static void json_stringify_nv_map(const NVMap* m, FILE* out, int depth) {
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
        json_stringify_value_recursive(&m->values[i], out, depth + 1);
    }
    fputs("}", out);
}

static void json_stringify_nv_tuple(const NVTuple* t, FILE* out, int depth) {
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
        json_stringify_value_recursive(&t->fields[i], out, depth + 1);
    }
    fputs("]", out);
}

static void json_stringify_value_recursive(const Value* v, FILE* out, int depth) {
    if (!v || !v->obj) {
        fputs("null", out);
        return;
    }

    // Limitar profundidade para evitar recursão excessiva
    if (depth > 10) {
        fputs("\"...[depth limit]...\"", out);
        return;
    }

    // Obter o tipo do objeto
    NvTypeObject* type = v->obj->ob_type;
    
    if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)v->obj;
        fprintf(out, "%d", int_obj->value);
    }
    else if (type == NVFloat_Type) {
        NVFloat* float_obj = (NVFloat*)v->obj;
        fprintf(out, "%.17g", float_obj->value);
    }
    else if (type == NVBool_Type) {
        NVBool* bool_obj = (NVBool*)v->obj;
        fputs(bool_obj->value ? "true" : "false", out);
    }
    else if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)v->obj;
        if (str_obj->value) {
            json_string_escape(str_obj->value, out);
        } else {
            fputs("null", out);
        }
    }
    else if (type == NVArray_Type) {
        NVArray* a = (NVArray*)v->obj;
        json_stringify_nv_array(a, out, depth);
    }
    else if (type == NVVector_Type) {
        NVVector* vec = (NVVector*)v->obj;
        json_stringify_nv_vector(vec, out, depth);
    }
    else if (type == NVMap_Type) {
        NVMap* m = (NVMap*)v->obj;
        json_stringify_nv_map(m, out, depth);
    }
    else if (type == NVTuple_Type) {
        NVTuple* t = (NVTuple*)v->obj;
        json_stringify_nv_tuple(t, out, depth);
    }
    else {
        // Tipo desconhecido, tratar como null ou string
        fputs("\"[unknown_type]\"", out);
    }
}

// Função principal de serialização JSON para string
void json_stringify(Value* out, const Value* v) {
    if (!out || !v) {
        create_str(out, strdup("null"));
        return;
    }

    // Resetar visited pointers
    visited_count = 0;

    // Estimar tamanho necessário (aproximado)
    size_t capacity = 1024;
    char* buffer = (char*)malloc(capacity);
    if (!buffer) {
        create_str(out, strdup("null"));
        return;
    }

    // Abrir arquivo temporário em memória usando tmpfile
    FILE* f = tmpfile();
    if (!f) {
        free(buffer);
        create_str(out, strdup("null"));
        return;
    }

    // Serializar para o arquivo temporário
    json_stringify_value_recursive(v, f, 0);
    
    // Ler de volta para o buffer
    rewind(f);
    size_t size = fread(buffer, 1, capacity - 1, f);
    buffer[size] = '\0';
    fclose(f);

    // Criar string com o resultado
    create_str(out, buffer);
}
