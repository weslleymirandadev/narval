#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Declaração da função de inicialização
extern void register_global_init(void);

void create_str(Value* out, const char* value) {
    if (!out) {
        return;
    }
    
    // Verificar se o ponteiro value é válido
    if (!value) {
        value = "";
    }
    
    // Verificar se o ponteiro está em uma região válida (não é stack corrompido)
    if ((unsigned long)value < 0x1000) {
        // Criar string vazia como fallback
        value = "";
    }
    
    // Garantir que os tipos estejam inicializados
    if (!NVStr_Type) {
        register_global_init();
    }
    
    // Criar NVStr
    NVStr* str_obj = (NVStr*)calloc(1, sizeof(NVStr));
    
    if (!str_obj) {
        out->obj = NULL;
        return;
    }
    
    str_obj->ob_base.ob_type = NVStr_Type;
    str_obj->ob_base.ref_count = 1;
    str_obj->ob_base.flags = 0;
    
    str_obj->value = strdup(value);
    str_obj->len = strlen(value);
    
    out->obj = (NvObject*)str_obj;
}
