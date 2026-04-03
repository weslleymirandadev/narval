#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>

void create_vector(Value* out) {
    if (!out) return;
    
    // Criar NVVector
    NVVector* vector_obj = (NVVector*)calloc(1, sizeof(NVVector));
    if (!vector_obj) {
        out->obj = NULL;
        return;
    }
    
    vector_obj->ob_base.ob_type = NVVector_Type;
    vector_obj->ob_base.ref_count = 1;
    vector_obj->ob_base.flags = 0;
    vector_obj->elements = NULL;
    vector_obj->size = 0;
    vector_obj->capacity = 0;
    
    out->obj = (NvObject*)vector_obj;
}
