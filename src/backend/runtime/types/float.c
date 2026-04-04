#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

void create_float(Value* out, double value) {
    if (!out) return;
    
    // Limpar struct para evitar problemas
    memset(out, 0, sizeof(Value));
    
    // Se os tipos não estiverem inicializados, criar objeto básico
    if (!NVFloat_Type) {
        // Criar NVFloat básico sem tipo por enquanto
        NVFloat* float_obj = (NVFloat*)calloc(1, sizeof(NVFloat));
        if (!float_obj) {
            out->obj = NULL;
            return;
        }
        
        // Inicializar campos básicos
        float_obj->ob_base.ob_type = NULL;  // Sem tipo por enquanto
        float_obj->ob_base.ref_count = 1;
        float_obj->ob_base.flags = 0;
        float_obj->value = value;
        
        out->obj = (NvObject*)float_obj;
        return;
    }
    
    // Criar NVFloat normalmente
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
