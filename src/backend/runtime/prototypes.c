#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"

StringVTable string_vtable_instance = {
    string_to_upper_case,
    string_replace,
    string_includes
};

ArrayVTable array_vtable_instance = {};

VectorVTable vector_vtable = {
    vector_push_impl,
    vector_pop_impl,
    vector_get_impl,
    vector_set_impl,
};

MapVTable map_vtable = {
    map_get_impl,
    map_set_impl,
};