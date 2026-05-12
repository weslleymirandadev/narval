#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

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
        NVFloat* float_obj = (NVFloat*)input->obj;
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.15g", float_obj->value);
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
        NVArray* arr = (NVArray*)input->obj;
        size_t buf_size = 64;
        char* buf = (char*)malloc(buf_size);
        if (!buf) { create_str(out, "[]"); return; }
        size_t pos = 0;
        buf[pos++] = '{';
        for (int i = 0; i < arr->size; i++) {
            Value elem_str = {0};
            nv_str_convert(&elem_str, &arr->elements[i]);
            NVStr* s = (NVStr*)elem_str.obj;
            const char* sv = (s && s->value) ? s->value : "";
            size_t sv_len = strlen(sv);
            size_t need = pos + sv_len + 4;
            if (need > buf_size) {
                buf_size = need * 2;
                char* nb = (char*)realloc(buf, buf_size);
                if (!nb) { free(buf); create_str(out, "{...}"); return; }
                buf = nb;
            }
            memcpy(buf + pos, sv, sv_len);
            pos += sv_len;
            if (i < arr->size - 1) { buf[pos++] = ','; buf[pos++] = ' '; }
        }
        buf[pos++] = '}';
        buf[pos] = '\0';
        create_str(out, buf);
        free(buf);
    }
    else if (type == NVVector_Type) {
        NVVector* vec = (NVVector*)input->obj;
        size_t buf_size = 64;
        char* buf = (char*)malloc(buf_size);
        if (!buf) { create_str(out, "[]"); return; }
        size_t pos = 0;
        buf[pos++] = '[';
        for (int i = 0; i < vec->size; i++) {
            Value elem_str = {0};
            nv_str_convert(&elem_str, &vec->elements[i]);
            NVStr* s = (NVStr*)elem_str.obj;
            const char* sv = (s && s->value) ? s->value : "";
            size_t sv_len = strlen(sv);
            size_t need = pos + sv_len + 4;
            if (need > buf_size) {
                buf_size = need * 2;
                char* nb = (char*)realloc(buf, buf_size);
                if (!nb) { free(buf); create_str(out, "[...]"); return; }
                buf = nb;
            }
            memcpy(buf + pos, sv, sv_len);
            pos += sv_len;
            if (i < vec->size - 1) { buf[pos++] = ','; buf[pos++] = ' '; }
        }
        buf[pos++] = ']';
        buf[pos] = '\0';
        create_str(out, buf);
        free(buf);
    }
    else if (type == NVMap_Type) {
        NVMap* map = (NVMap*)input->obj;
        // Prefer instance class name if present
        Value clsname = {0};
        nv_object_get_field(&clsname, input, "__class_name__");
        if (clsname.obj && clsname.obj->ob_type == NVStr_Type) {
            NVStr* s = (NVStr*)clsname.obj;
            char buf[256];
            snprintf(buf, sizeof(buf), "<object:%s>", s->value ? s->value : "object");
            create_str(out, buf);
        } else if (map->str_method) {
            *out = map->str_method(input);
        } else {
            create_str(out, "<map object>");
        }
    }
    else if (type == NVTuple_Type) {
        create_str(out, "<tuple object>");
    }
    else if (type == NVOptionNone_Type) {
        create_str(out, "None");
    }
    else if (type == NVOptionSome_Type) {
        nv_str_convert(out, &((NVOptionSome*)input->obj)->inner);
    }
    else if (type == NVResultOk_Type) {
        nv_str_convert(out, &((NVResultOk*)input->obj)->inner);
    }
    else if (type == NVResultErr_Type) {
        nv_str_convert(out, &((NVResultErr*)input->obj)->inner);
    }
    else {
        const char* tname = type->tp_name ? type->tp_name : "object";
        char buf[256];
        snprintf(buf, sizeof(buf), "<object:%s>", tname);
        create_str(out, buf);
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
            if (*endptr == '\0' && endptr != str_obj->value) {
                create_int(out, (int32_t)val);
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "invalid literal for int(): '%s'", str_obj->value);
                nv_raise_value_error(msg);
            }
        } else {
            nv_raise_value_error("invalid literal for int(): ''");
        }
    }
    else {
        nv_raise_type_error("int() argument must be a str or number");
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
            if (*endptr == '\0' && endptr != str_obj->value) {
                create_float(out, val);
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "invalid literal for float(): '%s'", str_obj->value);
                nv_raise_value_error(msg);
            }
        } else {
            nv_raise_value_error("invalid literal for float(): ''");
        }
    }
    else {
        nv_raise_type_error("float() argument must be a str or number");
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
    memset(out, 0, sizeof(Value));
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%g", value);
    create_str(out, buffer);
}

