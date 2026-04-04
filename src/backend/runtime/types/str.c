#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Declaração da função de inicialização
extern void register_global_init(void);

void create_str(Value* out, const char* value) {
    if (!out) {
        return;
    }
    
    // Limpar struct para evitar problemas
    memset(out, 0, sizeof(Value));
    
    // Verificar se o ponteiro value é válido
    if (!value) {
        value = "";
    }
    
    // Verificar se o ponteiro está em uma região válida (não é stack corrompido)
    if ((unsigned long)value < 0x1000) {
        // Criar string vazia como fallback
        value = "";
    }
    
    // Se os tipos não estiverem inicializados, criar objeto básico
    if (!NVStr_Type) {
        // Criar NVStr básico sem tipo por enquanto
        NVStr* str_obj = (NVStr*)calloc(1, sizeof(NVStr));
        if (!str_obj) {
            out->obj = NULL;
            return;
        }
        
        // Inicializar campos básicos
        str_obj->ob_base.ob_type = NULL;  // Sem tipo por enquanto
        str_obj->ob_base.ref_count = 1;
        str_obj->ob_base.flags = 0;
        
        str_obj->value = strdup(value);
        str_obj->len = strlen(value);
        
        out->obj = (NvObject*)str_obj;
        return;
    }
    
    // Criar NVStr normalmente
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
