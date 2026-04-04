#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Definição real de NVType_Type para resolver undefined references
NvTypeObject* NVType_Type = NULL;

/* ============================================================= */
/*                    INICIALIZAÇÃO DO RUNTIME DO NARVAL        */
/* ============================================================= */

// Tipos builtin globais
NvTypeObject* NVInt_Type = NULL;
NvTypeObject* NVFloat_Type = NULL;
NvTypeObject* NVBool_Type = NULL;
NvTypeObject* NVStr_Type = NULL;
NvTypeObject* NVArray_Type = NULL;
NvTypeObject* NVVector_Type = NULL;
NvTypeObject* NVMap_Type = NULL;
NvTypeObject* NVTuple_Type = NULL;
NvTypeObject* NVObject_Type = NULL;

// Prototypes globais para tipos builtin (legado compatibilidade)
void* string_prototype = NULL;
void* array_prototype = NULL;
void* vector_prototype = NULL;
void* map_prototype = NULL;

// Registrar símbolos globais (para compatibilidade com compilador)
void register_global_init(void) {
    static int initialized = 0;
    if (initialized) {
        printf("DEBUG: register_global_init() already initialized, skipping\n");
        return;
    }
    initialized = 1;
    printf("DEBUG: register_global_init() called\n");

    // Primeiro inicializar o sistema de tipos
    nv_type_system_init();
    printf("DEBUG: nv_type_system_init() completed\n");
    
    // Criar o tipo 'type' primeiro sem usar NVType_Type
    printf("DEBUG: Creating NVType_Type\n");
    NvTypeObject* type_type = nv_type_new("type", NULL, 0);
    if (type_type) {
        // Configurar o tipo 'type' para apontar para si mesmo
        type_type->ob_base.ob_type = type_type;
        type_type->tp_metaclass = type_type;
        NVType_Type = type_type;
        printf("DEBUG: NVType_Type created at %p\n", NVType_Type);
    }
    
    // Agora criar os outros tipos usando NVType_Type já inicializado
    printf("DEBUG: Creating NVInt_Type\n");
    NVInt_Type = nv_type_new("int", NULL, 0);
    printf("DEBUG: NVInt_Type created at %p\n", NVInt_Type);
    
    printf("DEBUG: Creating NVFloat_Type\n");
    NVFloat_Type = nv_type_new("float", NULL, 0);
    printf("DEBUG: NVFloat_Type created at %p\n", NVFloat_Type);
    
    printf("DEBUG: Creating NVBool_Type\n");
    NVBool_Type = nv_type_new("bool", NULL, 0);
    printf("DEBUG: NVBool_Type created at %p\n", NVBool_Type);
    
    printf("DEBUG: Creating NVStr_Type\n");
    NVStr_Type = nv_type_new("str", NULL, 0);
    printf("DEBUG: NVStr_Type created at %p\n", NVStr_Type);
    
    printf("DEBUG: Creating NVArray_Type\n");
    NVArray_Type = nv_type_new("array", NULL, 0);
    printf("DEBUG: NVArray_Type created at %p\n", NVArray_Type);
    
    printf("DEBUG: Creating NVVector_Type\n");
    NVVector_Type = nv_type_new("vector", NULL, 0);
    printf("DEBUG: NVVector_Type created at %p\n", NVVector_Type);
    
    printf("DEBUG: Creating NVMap_Type\n");
    NVMap_Type = nv_type_new("map", NULL, 0);
    printf("DEBUG: NVMap_Type created at %p\n", NVMap_Type);
    
    printf("DEBUG: Creating NVTuple_Type\n");
    NVTuple_Type = nv_type_new("tuple", NULL, 0);
    printf("DEBUG: NVTuple_Type created at %p\n", NVTuple_Type);
    
    printf("DEBUG: Creating NVObject_Type\n");
    NVObject_Type = nv_type_new("object", NULL, 0);
    printf("DEBUG: NVObject_Type created at %p\n", NVObject_Type);
    
    printf("DEBUG: register_global_init() completed\n");
}
