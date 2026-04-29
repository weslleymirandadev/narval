#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <string.h>

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

int32_t nv_value_cmp(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return 0;
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (!ta || !tb) return 0;

    if (ta == NVInt_Type && tb == NVInt_Type) {
        int32_t va = ((NVInt*)a->obj)->value;
        int32_t vb = ((NVInt*)b->obj)->value;
        return (va > vb) - (va < vb);
    }
    if (ta == NVFloat_Type && tb == NVFloat_Type) {
        double va = ((NVFloat*)a->obj)->value;
        double vb = ((NVFloat*)b->obj)->value;
        if (va < vb) return -1;
        if (va > vb) return  1;
        return 0;
    }
    /* mixed int/float */
    double va = (ta == NVFloat_Type) ? ((NVFloat*)a->obj)->value : (double)((NVInt*)a->obj)->value;
    double vb = (tb == NVFloat_Type) ? ((NVFloat*)b->obj)->value : (double)((NVInt*)b->obj)->value;
    if (va < vb) return -1;
    if (va > vb) return  1;
    return 0;
}

char* extract_string_from_value(Value* v) {
    if (!v || !v->obj) return "";
    NvTypeObject* type = v->obj->ob_type;
    if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)v->obj;
        return str_obj->value ? str_obj->value : "";
    }
    return "";
}
