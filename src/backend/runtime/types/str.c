#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

void create_str(Value* out, const char* value) {
    if (!out) return;
    
    // Criar NVStr
    NVStr* str_obj = (NVStr*)calloc(1, sizeof(NVStr));
    if (!str_obj) {
        out->obj = NULL;
        return;
    }
    
    str_obj->ob_base.ob_type = NVStr_Type;
    str_obj->ob_base.ref_count = 1;
    str_obj->ob_base.flags = 0;
    
    if (value) {
        str_obj->value = strdup(value);
        str_obj->len = strlen(value);
    } else {
        str_obj->value = strdup("");
        str_obj->len = 0;
    }
    
    out->obj = (NvObject*)str_obj;
}
