#include "backend/runtime/prototypes.h"
#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

NvTypeObject* NVOptionNone_Type = NULL;
NvTypeObject* NVOptionSome_Type = NULL;
NvTypeObject* NVResultOk_Type   = NULL;
NvTypeObject* NVResultErr_Type  = NULL;

extern NvTypeObject* nv_type_new(const char* name, NvTypeObject** bases, int base_count);

void initialize_option_result_types(void) {
    NVOptionNone_Type = nv_type_new("Option::None", NULL, 0);
    NVOptionSome_Type = nv_type_new("Option::Some", NULL, 0);
    NVResultOk_Type   = nv_type_new("Result::Ok",   NULL, 0);
    NVResultErr_Type  = nv_type_new("Result::Err",  NULL, 0);

    if (NVOptionNone_Type) NVOptionNone_Type->tp_basicsize = sizeof(NVOptionNone);
    if (NVOptionSome_Type) NVOptionSome_Type->tp_basicsize = sizeof(NVOptionSome);
    if (NVResultOk_Type)   NVResultOk_Type->tp_basicsize   = sizeof(NVResultOk);
    if (NVResultErr_Type)  NVResultErr_Type->tp_basicsize   = sizeof(NVResultErr);
}

void create_option_some(Value* out, Value* val) {
    if (!out) return;
    if (!NVOptionSome_Type) { out->obj = NULL; return; }
    NVOptionSome* obj = (NVOptionSome*)calloc(1, sizeof(NVOptionSome));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type   = NVOptionSome_Type;
    obj->ob_base.ref_count = 1;
    obj->ob_base.flags     = 0;
    obj->inner = val ? *val : (Value){NULL};
    out->obj = (NvObject*)obj;
}

void create_option_none(Value* out) {
    if (!out) return;
    if (!NVOptionNone_Type) { out->obj = NULL; return; }
    NVOptionNone* obj = (NVOptionNone*)calloc(1, sizeof(NVOptionNone));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type   = NVOptionNone_Type;
    obj->ob_base.ref_count = 1;
    obj->ob_base.flags     = 0;
    out->obj = (NvObject*)obj;
}

void create_result_ok(Value* out, Value* val) {
    if (!out) return;
    if (!NVResultOk_Type) { out->obj = NULL; return; }
    NVResultOk* obj = (NVResultOk*)calloc(1, sizeof(NVResultOk));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type   = NVResultOk_Type;
    obj->ob_base.ref_count = 1;
    obj->ob_base.flags     = 0;
    obj->inner = val ? *val : (Value){NULL};
    out->obj = (NvObject*)obj;
}

void create_result_err(Value* out, Value* err) {
    if (!out) return;
    if (!NVResultErr_Type) { out->obj = NULL; return; }
    NVResultErr* obj = (NVResultErr*)calloc(1, sizeof(NVResultErr));
    if (!obj) { out->obj = NULL; return; }
    obj->ob_base.ob_type   = NVResultErr_Type;
    obj->ob_base.ref_count = 1;
    obj->ob_base.flags     = 0;
    obj->inner = err ? *err : (Value){NULL};
    out->obj = (NvObject*)obj;
}

int nv_is_failure(const Value* val) {
    if (!val || !val->obj || !val->obj->ob_type) return 0;
    NvTypeObject* t = val->obj->ob_type;
    return (t == NVOptionNone_Type || t == NVResultErr_Type) ? 1 : 0;
}

void nv_unwrap_inner(Value* out, const Value* val) {
    if (!out) return;
    if (!val || !val->obj || !val->obj->ob_type) {
        *out = val ? *val : (Value){NULL};
        return;
    }
    NvTypeObject* t = val->obj->ob_type;
    if (t == NVOptionSome_Type) {
        *out = ((NVOptionSome*)val->obj)->inner;
    } else if (t == NVResultOk_Type) {
        *out = ((NVResultOk*)val->obj)->inner;
    } else {
        *out = *val;
    }
}

/* Forward declarations */
extern NvTypeObject* NVError_Type;
extern NvTypeObject* NVStr_Type;
void nv_str_convert(Value* out, Value* input);

static int nv_is_error_type(NvTypeObject* t) {
    if (!t || !NVError_Type) return 0;
    /* Walk bases to check inheritance */
    if (t == NVError_Type) return 1;
    for (int i = 0; i < t->tp_bases_count; i++) {
        if (nv_is_error_type(t->tp_bases[i])) return 1;
    }
    return 0;
}

/* Always returns an Error Value so propagate/throw work correctly */
void nv_get_failure_err(Value* out, const Value* val) {
    if (!out) return;
    if (!val || !val->obj || !val->obj->ob_type) {
        create_error(out, "unknown error");
        return;
    }
    NvTypeObject* t = val->obj->ob_type;

    if (t == NVResultErr_Type) {
        Value inner = ((NVResultErr*)val->obj)->inner;
        /* If inner is already an Error, use it directly */
        if (inner.obj && inner.obj->ob_type && nv_is_error_type(inner.obj->ob_type)) {
            *out = inner;
            return;
        }
        /* Otherwise convert to Error with the string representation */
        Value msg_val = {NULL};
        nv_str_convert(&msg_val, &inner);
        const char* msg = "error";
        if (msg_val.obj && msg_val.obj->ob_type && NVStr_Type &&
            msg_val.obj->ob_type == NVStr_Type) {
            char* s = ((struct { NvObject_HEAD; char* value; size_t len; }*)msg_val.obj)->value;
            if (s) msg = s;
        }
        create_error(out, msg);
    } else if (t == NVOptionNone_Type) {
        create_error(out, "None");
    } else {
        create_error(out, "error");
    }
}
