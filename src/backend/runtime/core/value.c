// core/value.c — Value lifecycle: creation, extraction, comparison, copy.
// Merges: value_creation.c + value_extract.c + value_utils.c

#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

extern void register_global_init(void);

// ── Type identification ────────────────────────────────────────────────────

int32_t get_value_type(const Value* v) {
    if (!v || !v->obj) return 0;
    NvTypeObject* type = v->obj->ob_type;
    if (!type) return 0;
    if (type == NVInt_Type)         return NV_INT_BASE;
    if (type == NVFloat_Type)       return NV_FLOAT_BASE;
    if (type == NVBool_Type)        return NV_BOOL_BASE;
    if (type == NVChar_Type)        return NV_CHAR_BASE;
    if (type == NVStr_Type)         return NV_STR_BASE;
    if (type == NVArray_Type)       return NV_ARRAY_BASE;
    if (type == NVVector_Type)      return NV_VECTOR_BASE;
    if (type == NVMap_Type)         return NV_MAP_BASE;
    if (type == NVTuple_Type)       return NV_TUPLE_BASE;
    if (type == NVObject_Type)      return NV_OBJECT_BASE;
    if (type == NVType_Type)        return NV_TYPE_BASE;
    if (type == NVOptionNone_Type)  return NV_OPTION_NONE_BASE;
    if (type == NVOptionSome_Type)  return NV_OPTION_SOME_BASE;
    if (type == NVResultOk_Type)    return NV_RESULT_OK_BASE;
    if (type == NVResultErr_Type)   return NV_RESULT_ERR_BASE;
    if (type == NVFuture_Type)      return NV_FUTURE_BASE;
    return NV_ANY_BASE;
}

void ensure_value_type(Value* v) { (void)v; }

int validate_value_type(const Value* v, int32_t expected_type) {
    if (!v || !v->obj) return 0;
    return get_value_type(v) == expected_type;
}

void free_value(Value* v) {
    if (!v || !v->obj) return;
    nv_decref(v->obj);
    v->obj = NULL;
}

// ── Creation ───────────────────────────────────────────────────────────────

void create_int(Value* out, int32_t value) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    if (!NVInt_Type) register_global_init();
    NVInt* obj = (NVInt*)calloc(1, sizeof(NVInt));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type  = NVInt_Type;
    obj->ob_base.ref_count = 1;
    obj->value = value;
    out->obj = (NvObject*)obj;
}

void create_float(Value* out, double value) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    if (!NVFloat_Type) register_global_init();
    NVFloat* obj = (NVFloat*)calloc(1, sizeof(NVFloat));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type  = NVFloat_Type;
    obj->ob_base.ref_count = 1;
    obj->value = value;
    out->obj = (NvObject*)obj;
}

void create_bool(Value* out, int32_t value) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    if (!NVBool_Type) register_global_init();
    NVBool* obj = (NVBool*)calloc(1, sizeof(NVBool));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type  = NVBool_Type;
    obj->ob_base.ref_count = 1;
    obj->value = value ? 1 : 0;
    out->obj = (NvObject*)obj;
}

void create_char(Value* out, char value) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    if (!NVChar_Type) register_global_init();
    NVChar* obj = (NVChar*)calloc(1, sizeof(NVChar));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type  = NVChar_Type;
    obj->ob_base.ref_count = 1;
    obj->value = value;
    out->obj = (NvObject*)obj;
}

void create_str(Value* out, const char* value) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    if (!NVStr_Type) register_global_init();
    NVStr* obj = NVStr_Type
        ? (NVStr*)nv_object_new(NVStr_Type, NULL, NULL)
        : (NVStr*)calloc(1, sizeof(NVStr));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type  = NVStr_Type;
    obj->ob_base.ref_count = 1;
    obj->value = value ? strdup(value) : NULL;
    obj->len   = value ? strlen(value) : 0;
    out->obj = (NvObject*)obj;
}

void create_array(Value* out, int32_t size) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    NVArray* obj = NVArray_Type
        ? (NVArray*)nv_object_new(NVArray_Type, NULL, NULL)
        : (NVArray*)calloc(1, sizeof(NVArray));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type  = NVArray_Type;
    obj->ob_base.ref_count = 1;
    if (size > 0) {
        obj->elements = (Value*)calloc(size, sizeof(Value));
        if (obj->elements) { obj->capacity = size; obj->size = size; }
    }
    out->obj = (NvObject*)obj;
}

void create_vector(Value* out, int32_t capacity) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    NVVector* obj = NVVector_Type
        ? (NVVector*)nv_object_new(NVVector_Type, NULL, NULL)
        : (NVVector*)calloc(1, sizeof(NVVector));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type  = NVVector_Type;
    obj->ob_base.ref_count = 1;
    if (capacity > 0) {
        obj->elements = (Value*)malloc(capacity * sizeof(Value));
        if (obj->elements) obj->capacity = capacity;
    }
    out->obj = (NvObject*)obj;
}

void create_map(Value* out) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    NVMap* obj = NVMap_Type
        ? (NVMap*)nv_object_new(NVMap_Type, NULL, NULL)
        : (NVMap*)calloc(1, sizeof(NVMap));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type  = NVMap_Type;
    obj->ob_base.ref_count = 1;
    out->obj = (NvObject*)obj;
}

void create_tuple(Value* out, int32_t field_count) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    NVTuple* obj = NVTuple_Type
        ? (NVTuple*)nv_object_new(NVTuple_Type, NULL, NULL)
        : (NVTuple*)calloc(1, sizeof(NVTuple));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type  = NVTuple_Type;
    obj->ob_base.ref_count = 1;
    if (field_count > 0) {
        obj->fields = (Value*)calloc(field_count, sizeof(Value));
        if (obj->fields) obj->field_count = field_count;
    }
    out->obj = (NvObject*)obj;
}

void create_any(Value* out) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    if (!NVObject_Type) { out->obj = NULL; return; }
    out->obj = nv_object_new(NVObject_Type, NULL, NULL);
}

void tuple_set_impl(Value* self, int32_t index, Value* elem) {
    if (!self || !self->obj || !elem) return;
    NVTuple* t = (NVTuple*)self->obj;
    if (index < 0 || index >= t->field_count) return;
    t->fields[index] = *elem;
}

void tuple_get_impl(Value* out, Value* self, int32_t index) {
    if (!out) return;
    if (!self || !self->obj) { out->obj = NULL; return; }
    NVTuple* t = (NVTuple*)self->obj;
    if (index < 0 || index >= t->field_count) { out->obj = NULL; return; }
    *out = t->fields[index];
}

// ── Extraction ─────────────────────────────────────────────────────────────

int32_t extract_int_from_value(Value* v) {
    if (!v || !v->obj) return 0;
    NvTypeObject* type = v->obj->ob_type;
    if (!type) return 0;
    if (type == NVInt_Type)   return ((NVInt*)v->obj)->value;
    if (type == NVChar_Type)  return (int32_t)(unsigned char)((NVChar*)v->obj)->value;
    if (type == NVBool_Type)  return ((NVBool*)v->obj)->value ? 1 : 0;
    if (type == NVFloat_Type) return (int32_t)((NVFloat*)v->obj)->value;
    return 0;
}

char extract_char_from_value(Value* v) {
    if (!v || !v->obj) return '\0';
    NvTypeObject* type = v->obj->ob_type;
    if (type == NVChar_Type) return ((NVChar*)v->obj)->value;
    return (char)extract_int_from_value(v);
}

double extract_float_from_value(Value* v) {
    if (!v || !v->obj) return 0.0;
    NvTypeObject* type = v->obj->ob_type;
    if (type == NVFloat_Type) return ((NVFloat*)v->obj)->value;
    if (type == NVInt_Type)   return (double)((NVInt*)v->obj)->value;
    if (type == NVChar_Type)  return (double)(unsigned char)((NVChar*)v->obj)->value;
    return 0.0;
}

int32_t nv_value_cmp(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return 0;
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (!ta || !tb) return 0;

    if (ta == NVInt_Type && tb == NVInt_Type) {
        int32_t va = ((NVInt*)a->obj)->value;
        int32_t vb = ((NVInt*)b->obj)->value;
        return (va > vb) - (va < vb);
    }
    if (ta == NVChar_Type && tb == NVChar_Type) {
        unsigned char va = (unsigned char)((NVChar*)a->obj)->value;
        unsigned char vb = (unsigned char)((NVChar*)b->obj)->value;
        return (va > vb) - (va < vb);
    }
    // Mixed numeric: promote to double
    if ((ta == NVInt_Type || ta == NVFloat_Type) &&
        (tb == NVInt_Type || tb == NVFloat_Type)) {
        double va = ta == NVFloat_Type ? ((NVFloat*)a->obj)->value : (double)((NVInt*)a->obj)->value;
        double vb = tb == NVFloat_Type ? ((NVFloat*)b->obj)->value : (double)((NVInt*)b->obj)->value;
        return (va > vb) - (va < vb);
    }
    return strcmp(ta->tp_name ? ta->tp_name : "", tb->tp_name ? tb->tp_name : "");
}

char* extract_string_from_value(Value* v) {
    if (!v || !v->obj) return "";
    NvTypeObject* type = v->obj->ob_type;
    if (type == NVStr_Type) {
        NVStr* s = (NVStr*)v->obj;
        return s->value ? s->value : "";
    }
    Value str_val = {0};
    nv_str_convert(&str_val, v);
    if (str_val.obj && str_val.obj->ob_type == NVStr_Type)
        return ((NVStr*)str_val.obj)->value ? ((NVStr*)str_val.obj)->value : "";
    return "";
}

// ── Copy / compare (legacy compat) ────────────────────────────────────────

void copy_value(Value* dest, const Value* src) {
    if (!dest || !src) return;
    if (!src->obj) { dest->obj = NULL; return; }
    nv_incref(src->obj);
    dest->obj = src->obj;
}

int compare_values(const Value* a, const Value* b) {
    if (!a || !b) return 0;
    if (!a->obj && !b->obj) return 0;
    if (!a->obj) return -1;
    if (!b->obj) return 1;
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (ta != tb) return (ta < tb) ? -1 : 1;
    if (ta == NVInt_Type) {
        int32_t va = ((NVInt*)a->obj)->value, vb = ((NVInt*)b->obj)->value;
        return (va > vb) - (va < vb);
    }
    if (ta == NVFloat_Type) {
        double va = ((NVFloat*)a->obj)->value, vb = ((NVFloat*)b->obj)->value;
        return (va > vb) - (va < vb);
    }
    if (ta == NVStr_Type) {
        const char* sa = ((NVStr*)a->obj)->value ? ((NVStr*)a->obj)->value : "";
        const char* sb = ((NVStr*)b->obj)->value ? ((NVStr*)b->obj)->value : "";
        return strcmp(sa, sb);
    }
    return (a->obj < b->obj) ? -1 : (a->obj > b->obj) ? 1 : 0;
}

int is_null_value(const Value* v)  { return !v || !v->obj; }
void create_null(Value* out)       { if (out) out->obj = NULL; }
int validate_value(const Value* v) { if (!v || !v->obj) return 0; return is_valid_type(get_value_type(v)); }

TypeInfo* get_value_type_info(const Value* v)         { (void)v; return NULL; }
TypeInfo* get_value_type_info_legacy(const Value* v)  { (void)v; return NULL; }
void print_type_info(const Value* v)                  { (void)v; }

void free_type_info(TypeInfo* info) {
    if (!info) return;
    if (info->type_name) free((void*)info->type_name);
    if (info->field_names) {
        for (int i = 0; i < info->field_count; i++) free(info->field_names[i]);
        free(info->field_names);
    }
    if (info->field_types) free(info->field_types);
    free(info);
}

int convert_type(Value* out, const Value* in, int32_t target_type) {
    if (!out || !in || !in->obj) return 0;
    int32_t src = get_value_type(in);
    if (src == target_type) { *out = *in; return 1; }
    switch (target_type) {
        case NV_INT_BASE:   create_int(out, (int32_t)extract_float_from_value((Value*)in)); return 1;
        case NV_FLOAT_BASE: create_float(out, extract_float_from_value((Value*)in)); return 1;
        case NV_BOOL_BASE:  create_bool(out, extract_int_from_value((Value*)in) != 0); return 1;
        default: return 0;
    }
}
