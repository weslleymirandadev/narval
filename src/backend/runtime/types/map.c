#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

void create_map(Value* out) {
    if (!out) return;
    
    // Criar NVMap
    NVMap* map_obj = (NVMap*)calloc(1, sizeof(NVMap));
    if (!map_obj) {
        out->obj = NULL;
        return;
    }
    
    map_obj->ob_base.ob_type = NVMap_Type;
    map_obj->ob_base.ref_count = 1;
    map_obj->ob_base.flags = 0;
    map_obj->keys = NULL;
    map_obj->values = NULL;
    map_obj->size = 0;
    map_obj->capacity = 0;
    
    out->obj = (NvObject*)map_obj;
}
