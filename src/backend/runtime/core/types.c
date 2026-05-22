// core/types.c — Type system: registry, builder, and legacy prototype stubs.
// Merges: type_registry.c + type_builder.c + prototypes.c

#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

// ── Global type registry ───────────────────────────────────────────────────

TypeRegistry* global_type_registry = NULL;

void nv_type_system_init(void) {
    if (global_type_registry) return;
    global_type_registry = (TypeRegistry*)calloc(1, sizeof(TypeRegistry));
    global_type_registry->capacity = 16;
    global_type_registry->types = (NvTypeObject**)calloc(16, sizeof(NvTypeObject*));
}

NvTypeObject* nv_type_new(const char* name, NvTypeObject** bases, int bases_count) {
    NvTypeObject* type = (NvTypeObject*)calloc(1, sizeof(NvTypeObject));
    if (!type) return NULL;
    type->ob_base.ob_type  = (strcmp(name, "type") == 0) ? NULL : NVType_Type;
    type->ob_base.ref_count = 1;
    type->tp_name       = name ? strdup(name) : "unknown";
    type->tp_basicsize  = sizeof(NvObject);
    type->tp_bases      = bases;
    type->tp_bases_count = bases_count;
    return type;
}

NvObject* nv_object_new(NvTypeObject* type, void* args, void* kwargs) {
    if (!type) return NULL;
    NvObject* obj = (NvObject*)calloc(1, type->tp_basicsize);
    if (!obj) return NULL;
    obj->ob_type   = type;
    obj->ref_count = 1;
    return obj;
}

NvTypeObject* nv_create_simple_class(const char* name) {
    return nv_type_new(name, NULL, 0);
}

void init_type_registry(void) { /* delegated to nv_type_system_init */ }

int32_t register_custom_type(TypeInfo* type_info) {
    if (!type_info || !global_type_registry) return -1;
    NvTypeObject* new_type = (NvTypeObject*)calloc(1, sizeof(NvTypeObject));
    if (!new_type) return -1;
    new_type->ob_base.ob_type  = NVType_Type;
    new_type->ob_base.ref_count = 1;
    new_type->tp_name      = type_info->type_name ? strdup(type_info->type_name) : "custom";
    new_type->tp_basicsize = sizeof(NVObject);
    new_type->tp_bases     = (NvTypeObject**)calloc(1, sizeof(NvTypeObject*));
    new_type->tp_bases[0]  = NVObject_Type;
    new_type->tp_bases_count = 1;
    new_type->tp_metaclass = NVType_Type;
    new_type->tp_cache     = type_info;
    if (global_type_registry->count >= global_type_registry->capacity) {
        global_type_registry->capacity *= 2;
        global_type_registry->types = (NvTypeObject**)realloc(
            global_type_registry->types,
            global_type_registry->capacity * sizeof(NvTypeObject*));
    }
    int32_t type_id = NV_TYPE_BASE + global_type_registry->count;
    global_type_registry->types[global_type_registry->count++] = new_type;
    return type_id;
}

TypeInfo* get_type_info(int32_t type_id) {
    if (!global_type_registry || type_id < NV_TYPE_BASE) return NULL;
    int index = type_id - NV_TYPE_BASE;
    if (index >= global_type_registry->count) return NULL;
    NvTypeObject* t = global_type_registry->types[index];
    return t ? (TypeInfo*)t->tp_cache : NULL;
}

TypeInfo* get_type_info_by_name(const char* name) {
    if (!global_type_registry || !name) return NULL;
    for (int i = 0; i < global_type_registry->count; i++) {
        NvTypeObject* t = global_type_registry->types[i];
        if (t && t->tp_name && strcmp(t->tp_name, name) == 0)
            return (TypeInfo*)t->tp_cache;
    }
    return NULL;
}

int is_valid_type(int32_t type) {
    return (type >= NV_INT_BASE && type <= NV_ANY_BASE) || type == NV_CHAR_BASE;
}

const char* get_type_name(int32_t type) {
    switch (type) {
        case NV_INT_BASE:    return "int";
        case NV_FLOAT_BASE:  return "float";
        case NV_BOOL_BASE:   return "bool";
        case NV_CHAR_BASE:   return "char";
        case NV_STR_BASE:    return "str";
        case NV_ARRAY_BASE:  return "array";
        case NV_VECTOR_BASE: return "vector";
        case NV_MAP_BASE:    return "map";
        case NV_TUPLE_BASE:  return "tuple";
        case NV_TYPE_BASE:   return "type";
        case NV_OBJECT_BASE: return "object";
        case NV_ANY_BASE:    return "any";
        default:             return "unknown";
    }
}

// ── TypeBuilder ────────────────────────────────────────────────────────────

struct TypeBuilder {
    char*           name;
    NvTypeObject**  bases;
    int             bases_count;
    int             bases_capacity;
    NvNumberMethods*   number_methods;
    NvSequenceMethods* sequence_methods;
    NvMappingMethods*  mapping_methods;
    Value**         methods;
    int             methods_count;
    int             methods_capacity;
    size_t          basicsize;
};

TypeBuilder* type_builder_new(const char* name) {
    if (!name) return NULL;
    TypeBuilder* b = (TypeBuilder*)calloc(1, sizeof(TypeBuilder));
    if (!b) return NULL;
    b->name            = strdup(name);
    b->bases_capacity  = 4;
    b->bases           = (NvTypeObject**)calloc(4, sizeof(NvTypeObject*));
    b->methods_capacity = 8;
    b->methods         = (Value**)calloc(8, sizeof(Value*));
    b->basicsize       = sizeof(NvObject);
    return b;
}

void type_builder_add_base(TypeBuilder* b, NvTypeObject* base) {
    if (!b || !base) return;
    if (b->bases_count >= b->bases_capacity) {
        b->bases_capacity *= 2;
        b->bases = (NvTypeObject**)realloc(b->bases, b->bases_capacity * sizeof(NvTypeObject*));
    }
    b->bases[b->bases_count++] = base;
}

void type_builder_add_method(TypeBuilder* b, const char* name, Value* method) {
    if (!b || !name || !method) return;
    if (b->methods_count >= b->methods_capacity) {
        b->methods_capacity *= 2;
        b->methods = (Value**)realloc(b->methods, b->methods_capacity * sizeof(Value*));
    }
    b->methods[b->methods_count++] = method;
}

void type_builder_set_number_protocol(TypeBuilder* b, NvNumberMethods* m)   { if (b) b->number_methods   = m; }
void type_builder_set_sequence_protocol(TypeBuilder* b, NvSequenceMethods* m) { if (b) b->sequence_methods = m; }
void type_builder_set_mapping_protocol(TypeBuilder* b, NvMappingMethods* m)  { if (b) b->mapping_methods  = m; }
void type_builder_set_basicsize(TypeBuilder* b, size_t size) { if (b) b->basicsize = size; }

NvTypeObject* type_builder_build(TypeBuilder* b) {
    if (!b || !b->name) return NULL;
    NvTypeObject* type = nv_type_new(b->name, b->bases, b->bases_count);
    if (!type) return NULL;
    type->tp_as_number   = b->number_methods;
    type->tp_as_sequence = b->sequence_methods;
    type->tp_as_mapping  = b->mapping_methods;
    if (b->methods_count > 0) {
        type->tp_methods       = (void**)b->methods;
        type->tp_methods_count = b->methods_count;
    }
    if (b->basicsize > 0) type->tp_basicsize = b->basicsize;
    if (NVType_Type) {
        type->ob_base.ob_type = NVType_Type;
        type->tp_metaclass    = NVType_Type;
    }
    free(b->name);
    free(b->bases);
    free(b);
    return type;
}

// ── Legacy prototype stubs ─────────────────────────────────────────────────
// The actual globals live in core/object.c (nv_runtime.c); only the stubs here.

void  init_prototypes(void)               { }
void* get_type_prototype(int32_t type_id) { (void)type_id; return NULL; }
int   has_prototype(int32_t type_id)      { (void)type_id; return 0; }
void  cleanup_prototypes(void)            { }
