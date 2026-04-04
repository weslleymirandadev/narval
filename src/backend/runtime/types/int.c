#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

void create_int(Value* out, int32_t value) {
    if (!out) return;
    
    // Limpar struct para evitar problemas
    memset(out, 0, sizeof(Value));
    
    // Se os tipos não estiverem inicializados, criar objeto básico
    if (!NVInt_Type) {
        // Criar NVInt básico sem tipo por enquanto
        NVInt* int_obj = (NVInt*)calloc(1, sizeof(NVInt));
        if (!int_obj) {
            out->obj = NULL;
            return;
        }
        
        // Inicializar campos básicos
        int_obj->ob_base.ob_type = NULL;  // Sem tipo por enquanto
        int_obj->ob_base.ref_count = 1;
        int_obj->ob_base.flags = 0;
        int_obj->value = value;
        
        out->obj = (NvObject*)int_obj;
        return;
    }
    
    // Criar NVInt normalmente
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
