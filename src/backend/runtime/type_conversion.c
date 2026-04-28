#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

/* ============================================================= */
/*                    FUNÇÕES DE CONVERSÃO DE TIPO             */
/* ============================================================= */

static void nv_i32_to_string(int32_t value, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (out_size == 1) { out[0] = '\0'; return; }
    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }

    char tmp[16];
    int i = 0;
    uint32_t v = (value < 0) ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    while (v > 0 && i < (int)sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }

    size_t pos = 0;
    if (value < 0 && pos < out_size - 1) out[pos++] = '-';
    while (i > 0 && pos < out_size - 1) out[pos++] = tmp[--i];
    out[pos] = '\0';
}

// str() - converte qualquer valor para string
void nv_str_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) {
        create_str(out, "None");
        return;
    }
    if ((uintptr_t)input->obj < 0x1000) {
        create_str(out, "None");
        return;
    }
    
    NvTypeObject* type = input->obj->ob_type;
    if (!type || (uintptr_t)type < 0x1000) {
        create_str(out, "None");
        return;
    }
    if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)input->obj;
        char buffer[32];
        nv_i32_to_string(int_obj->value, buffer, sizeof(buffer));
        create_str(out, buffer);
    }
    else if (type == NVFloat_Type) {
        // Conversão textual completa de float ainda será melhorada.
        create_str(out, "<float>");
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
        create_str(out, "<array object>");
    }
    else if (type == NVVector_Type) {
        create_str(out, "<vector object>");
    }
    else if (type == NVMap_Type) {
        create_str(out, "<map object>");
    }
    else if (type == NVTuple_Type) {
        create_str(out, "<tuple object>");
    }
    else {
        create_str(out, "<object>");
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
    if (!out) return;
    memset(out, 0, sizeof(Value));
    char buffer[32];
    nv_i32_to_string(value, buffer, sizeof(buffer));
    create_str(out, buffer);
}

// Converte float para string (versão simples para concatenação)
void float_to_string(Value* out, double value) {
    if (!out) return;
    
    // Limpar struct para evitar problemas com lixo de memória
    memset(out, 0, sizeof(Value));
    
    (void)value;
    create_str(out, "<float>");
}
