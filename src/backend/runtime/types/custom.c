#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Criar tipo customizado (placeholder genérico para tipos criados em tempo de compilação)
// Exemplo: type User = { name: string; password: string; }
void create_custom(Value* out, int32_t type_id, void* data) {
    if (!out) {
        fputs("FATAL: create_custom called with NULL pointer\n", stderr);
        return;
    }
    
    // Verificar se o tipo está registrado
    TypeInfo* info = get_type_info(type_id);
    if (!info) {
        fprintf(stderr, "ERROR: Custom type %d not registered\n", type_id);
        out->type = TAG_ANY;
        out->value = 0;
        out->prototype = NULL;
        out->type_info = NULL;
        out->flags = 0;
        return;
    }
    
    // Criar cópia dos dados se necessário
    void* data_copy = NULL;
    if (data && info->size > 0) {
        data_copy = malloc(info->size);
        if (!data_copy) {
            fprintf(stderr, "FATAL: malloc failed in create_custom\n");
            exit(1);
        }
        memcpy(data_copy, data, info->size);
    }
    
    out->type = type_id;
    out->value = (int64_t)(intptr_t)data_copy;
    out->prototype = NULL;
    out->type_info = info;
    out->flags = 0;
}

// Criar tipo customizado struct-like (helper para tipos como User acima)
// Aloca memória e inicializa campos baseado no TypeInfo
void create_custom_struct(Value* out, int32_t type_id, Value* field_values) {
    if (!out) {
        fputs("FATAL: create_custom_struct called with NULL pointer\n", stderr);
        return;
    }
    
    // Verificar se o tipo está registrado
    TypeInfo* info = get_type_info(type_id);
    if (!info) {
        fprintf(stderr, "ERROR: Custom type %d not registered\n", type_id);
        out->type = TAG_ANY;
        out->value = 0;
        out->prototype = NULL;
        out->type_info = NULL;
        out->flags = 0;
        return;
    }
    
    // Verificar se é um tipo struct-like
    if (!info->field_names || info->field_count == 0) {
        fprintf(stderr, "ERROR: Type %s is not a struct-like type\n", info->type_name ? info->type_name : "unknown");
        out->type = TAG_ANY;
        out->value = 0;
        out->prototype = NULL;
        out->type_info = NULL;
        out->flags = 0;
        return;
    }
    
    // Alocar memória para a estrutura
    // Para struct-like, armazenamos os Values diretamente em um array
    // O size do TypeInfo deve ser sizeof(Value) * field_count
    size_t struct_size = sizeof(Value) * info->field_count;
    Value* fields = (Value*)malloc(struct_size);
    if (!fields) {
        fprintf(stderr, "FATAL: malloc failed in create_custom_struct\n");
        exit(1);
    }
    
    // Inicializar campos
    if (field_values) {
        for (int i = 0; i < info->field_count; i++) {
            if (i < info->field_count && field_values[i].type != 0) {
                fields[i] = field_values[i];
            } else {
                // Campo não fornecido, inicializar com valor padrão
                fields[i] = (Value){0, 0, NULL, NULL, 0};
            }
        }
    } else {
        // Inicializar todos os campos com valores padrão
        memset(fields, 0, struct_size);
    }
    
    out->type = type_id;
    out->value = (int64_t)(intptr_t)fields;
    out->prototype = NULL;
    out->type_info = info;
    out->flags = 0;
}
