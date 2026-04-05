<<<<<<< HEAD
#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
=======
#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1

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
        return;
    }
    initialized = 1;

    // Primeiro inicializar o sistema de tipos
    nv_type_system_init();
<<<<<<< HEAD
    printf("[DEBUG] type system initialized\n");
    
    // Criar o tipo "type" (metaclass)
    NVType_Type = nv_type_new("type", NULL, 0);
    printf("[DEBUG] NVType_Type created at %p\n", (void*)NVType_Type);
    
    // Criar o tipo 'type' primeiro sem usar NVType_Type
    NvTypeObject* type_type = NVType_Type;
=======
    
    // Criar o tipo 'type' primeiro sem usar NVType_Type
    NvTypeObject* type_type = nv_type_new("type", NULL, 0);
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
    if (type_type) {
        // Configurar o tipo 'type' para apontar para si mesmo
        type_type->ob_base.ob_type = type_type;
        type_type->tp_metaclass = type_type;
<<<<<<< HEAD
=======
        NVType_Type = type_type;
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
    }
    
    // Agora criar os outros tipos usando NVType_Type já inicializado
    NVInt_Type = nv_type_new("int", NULL, 0);
<<<<<<< HEAD
    printf("[DEBUG] NVInt_Type created at %p\n", (void*)NVInt_Type);
    
    NVFloat_Type = nv_type_new("float", NULL, 0);
    printf("[DEBUG] NVFloat_Type created at %p\n", (void*)NVFloat_Type);
    
    NVStr_Type = nv_type_new("str", NULL, 0);
    printf("[DEBUG] NVStr_Type created at %p\n", (void*)NVStr_Type);
    
    NVBool_Type = nv_type_new("bool", NULL, 0);
    printf("[DEBUG] NVBool_Type created at %p\n", (void*)NVBool_Type);
    
    NVArray_Type = nv_type_new("array", NULL, 0);
    NVVector_Type = nv_type_new("vector", NULL, 0);
    NVMap_Type = nv_type_new("map", NULL, 0);
    NVTuple_Type = nv_type_new("tuple", NULL, 0);
    NVObject_Type = nv_type_new("object", NULL, 0);
    
    printf("[DEBUG] register_global_init completed\n");
=======
    
    NVFloat_Type = nv_type_new("float", NULL, 0);
    
    NVBool_Type = nv_type_new("bool", NULL, 0);
    
    NVStr_Type = nv_type_new("str", NULL, 0);
    
    NVArray_Type = nv_type_new("array", NULL, 0);
    
    NVVector_Type = nv_type_new("vector", NULL, 0);
    
    NVMap_Type = nv_type_new("map", NULL, 0);
    
    NVTuple_Type = nv_type_new("tuple", NULL, 0);
    
    NVObject_Type = nv_type_new("object", NULL, 0);    
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
}
