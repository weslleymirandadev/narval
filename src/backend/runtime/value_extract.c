#include <stdio.h>
#include "backend/runtime/prototypes.h"

// Função para extrair int de Value struct
int32_t extract_int_from_value(Value* v) {
    if (!v || !v->obj) return 0;
    
    // Verificar se o objeto é NVInt
    if (v->obj->ob_type == NVInt_Type) {
        NVInt* int_obj = (NVInt*)v->obj;
        return int_obj->value;
    }
    
    return 0;
}

// Função para extrair float de Value struct  
double extract_float_from_value(Value* v) {
    if (!v || !v->obj) return 0.0;
    
    // Verificar se o objeto é NVFloat
    if (v->obj->ob_type == NVFloat_Type) {
        NVFloat* float_obj = (NVFloat*)v->obj;
        return float_obj->value;
    }
    
    return 0.0;
}
