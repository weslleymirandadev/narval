#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>

void create_bool(Value* out, int32_t value) {
    if (!out) return;
    
    // Criar NVBool
    NVBool* bool_obj = (NVBool*)calloc(1, sizeof(NVBool));
    if (!bool_obj) {
        out->obj = NULL;
        return;
    }
    
    bool_obj->ob_base.ob_type = NVBool_Type;
    bool_obj->ob_base.ref_count = 1;
    bool_obj->ob_base.flags = 0;
    bool_obj->value = value ? 1 : 0;
    
    out->obj = (NvObject*)bool_obj;
}
