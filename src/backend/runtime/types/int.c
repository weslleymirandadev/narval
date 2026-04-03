#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>

void create_int(Value* out, int32_t value) {
    if (!out) return;
    
    // Criar NVInt
    NVInt* int_obj = (NVInt*)calloc(1, sizeof(NVInt));
    if (!int_obj) {
        out->obj = NULL;
        return;
    }
    
    int_obj->ob_base.ob_type = NVInt_Type;
    int_obj->ob_base.ref_count = 1;
    int_obj->ob_base.flags = 0;
    int_obj->value = value;
    
    out->obj = (NvObject*)int_obj;
}
