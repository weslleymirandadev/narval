#include "backend/runtime/prototypes.h"
#include <stdlib.h>
#include <string.h>

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
NvTypeObject* NVType_Type = NULL;

// Prototypes globais para tipos builtin (legado compatibilidade)
void* string_prototype = NULL;
void* array_prototype = NULL;
void* vector_prototype = NULL;
void* map_prototype = NULL;

// Registrar símbolos globais (para compatibilidade com compilador)
void register_global_init(void) {
    // Inicializar tipos básicos
    if (!NVInt_Type) {
        NVInt_Type = nv_type_new("int", NULL, 0);
    }
    if (!NVFloat_Type) {
        NVFloat_Type = nv_type_new("float", NULL, 0);
    }
    if (!NVBool_Type) {
        NVBool_Type = nv_type_new("bool", NULL, 0);
    }
    if (!NVStr_Type) {
        NVStr_Type = nv_type_new("str", NULL, 0);
    }
    if (!NVArray_Type) {
        NVArray_Type = nv_type_new("array", NULL, 0);
    }
    if (!NVVector_Type) {
        NVVector_Type = nv_type_new("vector", NULL, 0);
    }
    if (!NVMap_Type) {
        NVMap_Type = nv_type_new("map", NULL, 0);
    }
    if (!NVTuple_Type) {
        NVTuple_Type = nv_type_new("tuple", NULL, 0);
    }
    if (!NVObject_Type) {
        NVObject_Type = nv_type_new("object", NULL, 0);
    }
    if (!NVType_Type) {
        NVType_Type = nv_type_new("type", NULL, 0);
    }
}
