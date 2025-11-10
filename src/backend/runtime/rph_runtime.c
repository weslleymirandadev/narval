#include "backend/runtime/rph_runtime.h"
#include "backend/runtime/prototypes.h"

extern StringVTable string_vtable_instance;
extern ArrayVTable  array_vtable_instance;
extern VectorVTable vector_vtable;
extern MapVTable    map_vtable;

void* string_prototype = (void*)&string_vtable_instance;
void* array_prototype  = (void*)&array_vtable_instance;
void* vector_prototype = (void*)&vector_vtable;
void* map_prototype    = (void*)&map_vtable;