#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>

void create_float(Value* out, double value) {
    if (!out) return;
    
    // Criar NVFloat
    NVFloat* float_obj = (NVFloat*)calloc(1, sizeof(NVFloat));
    if (!float_obj) {
        out->obj = NULL;
        return;
    }
    
    float_obj->ob_base.ob_type = NVFloat_Type;
    float_obj->ob_base.ref_count = 1;
    float_obj->ob_base.flags = 0;
    float_obj->value = value;
    
    out->obj = (NvObject*)float_obj;
}
