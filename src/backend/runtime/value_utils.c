#include "backend/runtime/prototypes.h"
#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================= */
/*                    FUNÇÕES DE UTILIDADE - LEGADO ADAPTADO   */
/* ============================================================= */

// Obter informações de tipo de um Value (adaptado para novo sistema)
// Nota: Esta função já está implementada em value_creation.c
// Mantendo apenas para compatibilidade
TypeInfo* get_value_type_info_legacy(const Value* v) {
    if (!v || !v->obj) return NULL;
    
    // No novo sistema, obter tipo do objeto
    NvTypeObject* type = v->obj->ob_type;
    if (!type) return NULL;
    
    // Criar TypeInfo dinâmico para compatibilidade
    TypeInfo* info = (TypeInfo*)calloc(1, sizeof(TypeInfo));
    if (!info) return NULL;
    
    // Preencher informações básicas
    info->type_id = get_value_type(v);
    info->type_name = type->tp_name ? strdup(type->tp_name) : "unknown";
    info->field_count = 0;
    info->field_names = NULL;
    info->field_types = NULL;
    
    return info;
}

// Imprimir informações de tipo (para debug) - adaptado
void print_type_info(const Value* v) {
    if (!v || !v->obj) {
        printf("Value: NULL\n");
        return;
    }
    
    NvTypeObject* type = v->obj->ob_type;
    if (!type) {
        printf("Value: <unknown type>\n");
        return;
    }
    
    printf("Value type: %s\n", type->tp_name ? type->tp_name : "unknown");
    printf("  ref_count: %d\n", v->obj->ref_count);
    printf("  flags: 0x%x\n", v->obj->flags);
    
    // Mostrar valor específico
    if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)v->obj;
        printf("  value: %d\n", int_obj->value);
    } else if (type == NVFloat_Type) {
        NVFloat* float_obj = (NVFloat*)v->obj;
        printf("  value: %f\n", float_obj->value);
    } else if (type == NVBool_Type) {
        NVBool* bool_obj = (NVBool*)v->obj;
        printf("  value: %s\n", bool_obj->value ? "true" : "false");
    } else if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)v->obj;
        printf("  value: \"%s\" (len: %zu)\n", str_obj->value ? str_obj->value : "", str_obj->len);
    } else if (type == NVArray_Type) {
        NVArray* array_obj = (NVArray*)v->obj;
        printf("  size: %d, capacity: %d\n", array_obj->size, array_obj->capacity);
    } else if (type == NVVector_Type) {
        NVVector* vector_obj = (NVVector*)v->obj;
        printf("  size: %d, capacity: %d\n", vector_obj->size, vector_obj->capacity);
    } else if (type == NVMap_Type) {
        NVMap* map_obj = (NVMap*)v->obj;
        printf("  size: %d, capacity: %d\n", map_obj->size, map_obj->capacity);
    } else if (type == NVTuple_Type) {
        NVTuple* tuple_obj = (NVTuple*)v->obj;
        printf("  field_count: %d\n", tuple_obj->field_count);
    }
}

// Converter tipo (se possível) - adaptado para novo sistema
int convert_type(Value* out, const Value* in, int32_t target_type) {
    if (!out || !in || !in->obj) return 0;
    
    NvTypeObject* source_type = in->obj->ob_type;
    if (!source_type) return 0;
    
    int32_t source_type_id = get_value_type(in);
    
    // Se já é o tipo certo, copiar
    if (source_type_id == target_type) {
        *out = *in;
        return 1;
    }
    
    // Conversões básicas
    switch (target_type) {
        case NV_INT_BASE:
            if (source_type == NVFloat_Type) {
                NVFloat* float_obj = (NVFloat*)in->obj;
                create_int(out, (int32_t)float_obj->value);
                return 1;
            } else if (source_type == NVStr_Type) {
                NVStr* str_obj = (NVStr*)in->obj;
                if (str_obj->value) {
                    create_int(out, atoi(str_obj->value));
                    return 1;
                }
            } else if (source_type == NVBool_Type) {
                NVBool* bool_obj = (NVBool*)in->obj;
                create_int(out, bool_obj->value ? 1 : 0);
                return 1;
            }
            break;
            
        case NV_FLOAT_BASE:
            if (source_type == NVInt_Type) {
                NVInt* int_obj = (NVInt*)in->obj;
                create_float(out, (double)int_obj->value);
                return 1;
            } else if (source_type == NVStr_Type) {
                NVStr* str_obj = (NVStr*)in->obj;
                if (str_obj->value) {
                    create_float(out, atof(str_obj->value));
                    return 1;
                }
            } else if (source_type == NVBool_Type) {
                NVBool* bool_obj = (NVBool*)in->obj;
                create_float(out, bool_obj->value ? 1.0 : 0.0);
                return 1;
            }
            break;
            
        case NV_STR_BASE:
            if (source_type == NVInt_Type) {
                NVInt* int_obj = (NVInt*)in->obj;
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d", int_obj->value);
                create_str(out, buffer);
                return 1;
            } else if (source_type == NVFloat_Type) {
                NVFloat* float_obj = (NVFloat*)in->obj;
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "%f", float_obj->value);
                create_str(out, buffer);
                return 1;
            } else if (source_type == NVBool_Type) {
                NVBool* bool_obj = (NVBool*)in->obj;
                create_str(out, bool_obj->value ? "true" : "false");
                return 1;
            }
            break;
            
        case NV_BOOL_BASE:
            if (source_type == NVInt_Type) {
                NVInt* int_obj = (NVInt*)in->obj;
                create_bool(out, int_obj->value != 0);
                return 1;
            } else if (source_type == NVFloat_Type) {
                NVFloat* float_obj = (NVFloat*)in->obj;
                create_bool(out, float_obj->value != 0.0);
                return 1;
            } else if (source_type == NVStr_Type) {
                NVStr* str_obj = (NVStr*)in->obj;
                if (str_obj->value) {
                    // Converter string para booleano
                    if (strcmp(str_obj->value, "true") == 0 || strcmp(str_obj->value, "1") == 0) {
                        create_bool(out, 1);
                        return 1;
                    } else if (strcmp(str_obj->value, "false") == 0 || strcmp(str_obj->value, "0") == 0) {
                        create_bool(out, 0);
                        return 1;
                    }
                }
            }
            break;
    }
    
    // Conversão não suportada
    return 0;
}

// Validar valor (função legado adaptada)
int validate_value(const Value* v) {
    if (!v || !v->obj) return 0;
    
    // Verificar se o tipo é válido
    return is_valid_type(get_value_type(v));
}

// Copiar valor (função legado adaptada)
void copy_value(Value* dest, const Value* src) {
    if (!dest || !src) return;
    
    if (!src->obj) {
        dest->obj = NULL;
        return;
    }
    
    // Incrementar referência do objeto
    nv_incref(src->obj);
    dest->obj = src->obj;
}

// Comparar valores (função legado adaptada)
int compare_values(const Value* a, const Value* b) {
    if (!a || !b) return 0;
    if (!a->obj && !b->obj) return 0;
    if (!a->obj) return -1;
    if (!b->obj) return 1;
    
    NvTypeObject* type_a = a->obj->ob_type;
    NvTypeObject* type_b = b->obj->ob_type;
    
    // Tipos diferentes
    if (type_a != type_b) {
        return (type_a < type_b) ? -1 : 1;
    }
    
    // Comparação por tipo
    if (type_a == NVInt_Type) {
        NVInt* int_a = (NVInt*)a->obj;
        NVInt* int_b = (NVInt*)b->obj;
        if (int_a->value < int_b->value) return -1;
        if (int_a->value > int_b->value) return 1;
        return 0;
    } else if (type_a == NVFloat_Type) {
        NVFloat* float_a = (NVFloat*)a->obj;
        NVFloat* float_b = (NVFloat*)b->obj;
        if (float_a->value < float_b->value) return -1;
        if (float_a->value > float_b->value) return 1;
        return 0;
    } else if (type_a == NVStr_Type) {
        NVStr* str_a = (NVStr*)a->obj;
        NVStr* str_b = (NVStr*)b->obj;
        if (!str_a->value && !str_b->value) return 0;
        if (!str_a->value) return -1;
        if (!str_b->value) return 1;
        return strcmp(str_a->value, str_b->value);
    } else if (type_a == NVBool_Type) {
        NVBool* bool_a = (NVBool*)a->obj;
        NVBool* bool_b = (NVBool*)b->obj;
        if (bool_a->value < bool_b->value) return -1;
        if (bool_a->value > bool_b->value) return 1;
        return 0;
    }
    
    // Para outros tipos, comparar ponteiros
    if (a->obj < b->obj) return -1;
    if (a->obj > b->obj) return 1;
    return 0;
}

// Verificar se valor é nulo (função legado adaptada)
int is_null_value(const Value* v) {
    return !v || !v->obj;
}

// Criar valor nulo (função legado adaptada)
void create_null(Value* out) {
    if (!out) return;
    out->obj = NULL;
}

// Liberar TypeInfo (função legado adaptada)
void free_type_info(TypeInfo* info) {
    if (!info) return;
    
    if (info->type_name) {
        free((void*)info->type_name);
    }
    
    if (info->field_names) {
        for (int i = 0; i < info->field_count; i++) {
            if (info->field_names[i]) {
                free(info->field_names[i]);
            }
        }
        free(info->field_names);
    }
    
    if (info->field_types) {
        free(info->field_types);
    }
    
    free(info);
}
