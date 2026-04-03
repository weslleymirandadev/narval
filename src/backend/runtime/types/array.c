#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>

void create_array(Value* out) {
    if (!out) return;
    
    // Criar NVArray
    NVArray* array_obj = (NVArray*)calloc(1, sizeof(NVArray));
    if (!array_obj) {
        out->obj = NULL;
        return;
    }
    
    array_obj->ob_base.ob_type = NVArray_Type;
    array_obj->ob_base.ref_count = 1;
    array_obj->ob_base.flags = 0;
    
    // Inicializar com capacidade padrão
    array_obj->capacity = 8;
    array_obj->elements = (Value*)calloc(array_obj->capacity, sizeof(Value));
    array_obj->size = 0;
    
    out->obj = (NvObject*)array_obj;
}
