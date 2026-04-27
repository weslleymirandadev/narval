#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

/* ============================================================= */
/*                    FUNÇÕES DE CONVERSÃO DE TIPO             */
/* ============================================================= */

// str() - converte qualquer valor para string
void nv_str_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) {
        create_str(out, "None");
        return;
    }
    
    NvTypeObject* type = input->obj->ob_type;
    char buffer[256];
    
    if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)input->obj;
        snprintf(buffer, sizeof(buffer), "%d", int_obj->value);
        create_str(out, buffer);
    }
    else if (type == NVFloat_Type) {
        NVFloat* float_obj = (NVFloat*)input->obj;
        snprintf(buffer, sizeof(buffer), "%.17g", float_obj->value);
        create_str(out, buffer);
    }
    else if (type == NVBool_Type) {
        NVBool* bool_obj = (NVBool*)input->obj;
        create_str(out, bool_obj->value ? "true" : "false");
    }
    else if (type == NVStr_Type) {
        // Já é string, apenas copiar
        NVStr* str_obj = (NVStr*)input->obj;
        create_str(out, str_obj->value ? str_obj->value : "");
    }
    else if (type == NVArray_Type) {
        NVArray* array_obj = (NVArray*)input->obj;
        snprintf(buffer, sizeof(buffer), "<array object with %d elements>", array_obj->size);
        create_str(out, buffer);
    }
    else if (type == NVVector_Type) {
        NVVector* vector_obj = (NVVector*)input->obj;
        snprintf(buffer, sizeof(buffer), "<vector object with %d elements>", vector_obj->size);
        create_str(out, buffer);
    }
    else if (type == NVMap_Type) {
        NVMap* map_obj = (NVMap*)input->obj;
        snprintf(buffer, sizeof(buffer), "<map object with %d entries>", map_obj->size);
        create_str(out, buffer);
    }
    else if (type == NVTuple_Type) {
        NVTuple* tuple_obj = (NVTuple*)input->obj;
        snprintf(buffer, sizeof(buffer), "<tuple object with %d fields>", tuple_obj->field_count);
        create_str(out, buffer);
    }
    else {
        snprintf(buffer, sizeof(buffer), "<object of type %s>", type->tp_name ? type->tp_name : "unknown");
        create_str(out, buffer);
    }
}

// int() - converte qualquer valor para int
void nv_int_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) {
        create_int(out, 0);
        return;
    }
    
    NvTypeObject* type = input->obj->ob_type;
    
    if (type == NVInt_Type) {
        // Já é int, apenas copiar
        NVInt* int_obj = (NVInt*)input->obj;
        create_int(out, int_obj->value);
    }
    else if (type == NVFloat_Type) {
        NVFloat* float_obj = (NVFloat*)input->obj;
        create_int(out, (int32_t)float_obj->value);
    }
    else if (type == NVBool_Type) {
        NVBool* bool_obj = (NVBool*)input->obj;
        create_int(out, bool_obj->value ? 1 : 0);
    }
    else if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)input->obj;
        if (str_obj->value) {
            char* endptr;
            long val = strtol(str_obj->value, &endptr, 10);
            if (*endptr == '\0') {
                create_int(out, (int32_t)val);
            } else {
                create_int(out, 0);  // Erro de conversão, retorna 0 como Python
            }
        } else {
            create_int(out, 0);
        }
    }
    else {
        // Para outros tipos, tentar converter magicamente
        create_int(out, 0);
    }
}

// float() - converte qualquer valor para float
void nv_float_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) {
        create_float(out, 0.0);
        return;
    }
    
    NvTypeObject* type = input->obj->ob_type;
    
    if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)input->obj;
        create_float(out, (double)int_obj->value);
    }
    else if (type == NVFloat_Type) {
        // Já é float, apenas copiar
        NVFloat* float_obj = (NVFloat*)input->obj;
        create_float(out, float_obj->value);
    }
    else if (type == NVBool_Type) {
        NVBool* bool_obj = (NVBool*)input->obj;
        create_float(out, bool_obj->value ? 1.0 : 0.0);
    }
    else if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)input->obj;
        if (str_obj->value) {
            char* endptr;
            double val = strtod(str_obj->value, &endptr);
            if (*endptr == '\0') {
                create_float(out, val);
            } else {
                create_float(out, 0.0);  // Erro de conversão
            }
        } else {
            create_float(out, 0.0);
        }
    }
    else {
        // Para outros tipos
        create_float(out, 0.0);
    }
}

// bool() - converte qualquer valor para bool
void nv_bool_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) {
        create_bool(out, 0);
        return;
    }
    
    NvTypeObject* type = input->obj->ob_type;
    
    if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)input->obj;
        create_bool(out, int_obj->value != 0);
    }
    else if (type == NVFloat_Type) {
        NVFloat* float_obj = (NVFloat*)input->obj;
        create_bool(out, float_obj->value != 0.0);
    }
    else if (type == NVBool_Type) {
        // Já é bool, apenas copiar
        NVBool* bool_obj = (NVBool*)input->obj;
        create_bool(out, bool_obj->value);
    }
    else if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)input->obj;
        // Em Python, strings vazias são False, outras são True
        create_bool(out, str_obj->value && strlen(str_obj->value) > 0);
    }
    else if (type == NVArray_Type) {
        NVArray* array_obj = (NVArray*)input->obj;
        // Em Python, sequências vazias são False
        create_bool(out, array_obj->size > 0);
    }
    else if (type == NVVector_Type) {
        NVVector* vector_obj = (NVVector*)input->obj;
        create_bool(out, vector_obj->size > 0);
    }
    else if (type == NVMap_Type) {
        NVMap* map_obj = (NVMap*)input->obj;
        // Em Python, mappings vazios são False
        create_bool(out, map_obj->size > 0);
    }
    else if (type == NVTuple_Type) {
        NVTuple* tuple_obj = (NVTuple*)input->obj;
        create_bool(out, tuple_obj->field_count > 0);
    }
    else {
        // Para outros objetos, True (como em Python)
        create_bool(out, 1);
    }
}

// Converte inteiro para string (versão simples para concatenação)
void int_to_string(Value* out, int32_t value) {
    printf("[DEBUG] int_to_string called with value=%d\n", value);
    if (!out) {
        printf("[DEBUG] int_to_string: out is null\n");
        return;
    }
    memset(out, 0, sizeof(Value));
    printf("[DEBUG] int_to_string: after memset\n");
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    printf("[DEBUG] int_to_string: buffer='%s'\n", buffer);
    
    printf("[DEBUG] int_to_string: calling create_str\n");
    create_str(out, buffer);
}

// Converte float para string (versão simples para concatenação)
void float_to_string(Value* out, double value) {
    if (!out) return;
    
    // Limpar struct para evitar problemas com lixo de memória
    memset(out, 0, sizeof(Value));
    
    // Buffer temporário para conversão
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%g", value);
    
    // Criar string com o resultado
    create_str(out, buffer);
}
