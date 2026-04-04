#include "backend/runtime/nv_runtime.h"
#include <stdio.h>

int32_t extract_int_from_value(Value* v) {
    if (!v || !v->obj) {
        return 0;
    }
    
    NvTypeObject* type = v->obj->ob_type;
    if (!type) {
        return 0;
    }
    
    if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)v->obj;
        return int_obj->value;
    } else if (type == NVBool_Type) {
        NVBool* bool_obj = (NVBool*)v->obj;
        return bool_obj->value ? 1 : 0;
    }
    
    return 0;
}

double extract_float_from_value(Value* v) {
    if (!v || !v->obj) {
        return 0.0;
    }
    
    NvTypeObject* type = v->obj->ob_type;
    if (!type) {
        return 0.0;
    }
    
    if (type == NVFloat_Type) {
        NVFloat* float_obj = (NVFloat*)v->obj;
        return float_obj->value;
    } else if (type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)v->obj;
        return (double)int_obj->value;
    }
    
    return 0.0;
}

char* extract_string_from_value(Value* v) {
    if (!v || !v->obj) {
        return "";
    }
    
    NvTypeObject* type = v->obj->ob_type;
    if (!type) {
        return "";
    }
    
    if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)v->obj;
        return str_obj->value ? str_obj->value : "";
    }
    
    return "";
}
