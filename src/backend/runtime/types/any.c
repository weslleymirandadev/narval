#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

void create_any(Value* out) {
    if (!out) return;
    
    // Criar objeto any genérico
    NvObject* any_obj = (NvObject*)calloc(1, sizeof(NvObject));
    if (!any_obj) {
        out->obj = NULL;
        return;
    }
    
    any_obj->ob_type = NVObject_Type;
    any_obj->ref_count = 1;
    any_obj->flags = 0;
    
    out->obj = any_obj;
}
