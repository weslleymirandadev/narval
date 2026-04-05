#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
<<<<<<< HEAD
#include <string.h>
=======
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1

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
<<<<<<< HEAD
    printf("[DEBUG] extract_string_from_value called\n");
    if (!v || !v->obj) {
        printf("[DEBUG] extract_string_from_value: null pointer\n");
=======
    if (!v || !v->obj) {
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
        return "";
    }
    
    NvTypeObject* type = v->obj->ob_type;
<<<<<<< HEAD
    printf("[DEBUG] extract_string_from_value: type=%p, NVStr_Type=%p\n", (void*)type, (void*)NVStr_Type);
    
    // Só funcionar se tiver tipo NVStr_Type
    if (type == NVStr_Type) {
        printf("[DEBUG] extract_string_from_value: type is NVStr_Type\n");
        NVStr* str_obj = (NVStr*)v->obj;
        char* result = str_obj->value ? str_obj->value : "";
        printf("[DEBUG] extract_string_from_value: extracted='%s'\n", result);
        return result;
    }
    
    printf("[DEBUG] extract_string_from_value: type is not NVStr_Type, returning empty string\n");
=======
    if (!type) {
        return "";
    }
    
    if (type == NVStr_Type) {
        NVStr* str_obj = (NVStr*)v->obj;
        return str_obj->value ? str_obj->value : "";
    }
    
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
    return "";
}
