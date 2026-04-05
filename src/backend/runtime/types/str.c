#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
<<<<<<< HEAD
#include <stdio.h>
#include <string.h>
=======
#include <string.h>
#include <stdio.h>
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1

// Declaração da função de inicialização
extern void register_global_init(void);

void create_str(Value* out, const char* value) {
    if (!out) {
        return;
    }
    
<<<<<<< HEAD
    // Limpar struct para evitar problemas
    memset(out, 0, sizeof(Value));
    
    // Verificar se o ponteiro value é válido
    if (!value) { 
        value = ""; 
    }
    
    // Verificar se o ponteiro está em uma região válida (não é stack corrompido)
    if ((unsigned long)value < 0x1000) { 
        printf("[DEBUG] create_str: value looks like invalid pointer\n");
        value = ""; 
    }
    
    printf("[DEBUG] create_str: NVStr_Type=%p\n", (void*)NVStr_Type);
    
    // Se os tipos não estiverem inicializados, criar objeto básico
    if (!NVStr_Type) {
        printf("[DEBUG] create_str: NVStr_Type is null, creating basic object\n");
        // Criar NVStr básico sem tipo por enquanto
        NVStr* str_obj = (NVStr*)calloc(1, sizeof(NVStr));
        if (!str_obj) { 
            printf("[DEBUG] create_str: calloc failed\n");
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
        printf("[DEBUG] create_str: basic object created, value='%s'\n", str_obj->value);
        return;
    }
    
    printf("[DEBUG] create_str: creating normal NVStr object\n");
    // Criar NVStr normalmente
    NVStr* str_obj = (NVStr*)calloc(1, sizeof(NVStr));
    
    if (!str_obj) { 
        printf("[DEBUG] create_str: calloc failed\n");
        out->obj = NULL; 
        return; 
=======
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
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
    }
    
    str_obj->ob_base.ob_type = NVStr_Type;
    str_obj->ob_base.ref_count = 1;
    str_obj->ob_base.flags = 0;
    
    str_obj->value = strdup(value);
    str_obj->len = strlen(value);
    
    out->obj = (NvObject*)str_obj;
<<<<<<< HEAD
    printf("[DEBUG] create_str: normal object created, value='%s'\n", str_obj->value);
=======
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
}
