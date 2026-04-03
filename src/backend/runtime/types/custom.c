#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>

void create_custom(Value* out, const char* type_name) {
    if (!out || !type_name) return;
    
    // Criar tipo customizado dinamicamente
    NvTypeObject* custom_type = nv_create_simple_class(type_name);
    if (!custom_type) {
        out->obj = NULL;
        return;
    }
    
    // Criar instância do tipo customizado
    NvObject* custom_obj = nv_object_new(custom_type, NULL, NULL);
    if (!custom_obj) {
        out->obj = NULL;
        return;
    }
    
    out->obj = custom_obj;
}
