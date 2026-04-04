#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Declaração da função de inicialização
extern void register_global_init(void);

void create_str(Value* out, const char* value) {
    printf("DEBUG: create_str() called with out=%p, value=%s\n", out, value ? value : "NULL");
    
    if (!out) {
        printf("DEBUG: out is null, returning\n");
        return;
    }
    
    // Verificar se o ponteiro value é válido
    if (!value) {
        printf("DEBUG: value is null, creating empty string\n");
        value = "";
    }
    
    // Verificar se o ponteiro está em uma região válida (não é stack corrompido)
    if ((unsigned long)value < 0x1000) {
        printf("DEBUG: value pointer looks invalid (too small): %p\n", value);
        // Criar string vazia como fallback
        value = "";
    }
    
    // Garantir que os tipos estejam inicializados
    if (!NVStr_Type) {
        printf("DEBUG: NVStr_Type is null, calling register_global_init()\n");
        register_global_init();
        printf("DEBUG: NVStr_Type after init = %p\n", NVStr_Type);
    }
    
    // Criar NVStr
    printf("DEBUG: Allocating NVStr with size %zu\n", sizeof(NVStr));
    NVStr* str_obj = (NVStr*)calloc(1, sizeof(NVStr));
    printf("DEBUG: calloc returned str_obj = %p\n", str_obj);
    
    if (!str_obj) {
        printf("DEBUG: calloc failed, setting out->obj = NULL\n");
        out->obj = NULL;
        return;
    }
    
    printf("DEBUG: Setting ob_base.ob_type = %p\n", NVStr_Type);
    str_obj->ob_base.ob_type = NVStr_Type;
    str_obj->ob_base.ref_count = 1;
    str_obj->ob_base.flags = 0;
    
    printf("DEBUG: Copying string value: '%s'\n", value);
    str_obj->value = strdup(value);
    str_obj->len = strlen(value);
    
    printf("DEBUG: Setting out->obj = %p\n", str_obj);
    out->obj = (NvObject*)str_obj;
    
    // DEBUG: Verificar se foi criado corretamente
    if (out->obj && out->obj->ob_type) {
        printf("DEBUG: create_str() success - obj=%p, type=%p\n", out->obj, out->obj->ob_type);
    } else {
        printf("DEBUG: create_str() failed - obj=%p, type=%p\n", out->obj, out->obj ? out->obj->ob_type : NULL);
    }
}
