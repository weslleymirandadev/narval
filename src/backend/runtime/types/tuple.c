#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>

void create_tuple(Value* out) {
    if (!out) return;
    
    // Criar NVTuple
    NVTuple* tuple_obj = (NVTuple*)calloc(1, sizeof(NVTuple));
    if (!tuple_obj) {
        out->obj = NULL;
        return;
    }
    
    tuple_obj->ob_base.ob_type = NVTuple_Type;
    tuple_obj->ob_base.ref_count = 1;
    tuple_obj->ob_base.flags = 0;
    tuple_obj->fields = NULL;
    tuple_obj->field_count = 0;
    
    out->obj = (NvObject*)tuple_obj;
}
