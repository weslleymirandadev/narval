// ops/arithmetic.c — Type-aware arithmetic and operator dispatch.
// Merges: builtin_classes.c + operator_overload.c

#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

// ── Internal helpers ───────────────────────────────────────────────────────

static Value* builtin_add(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return NULL;
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (!ta || !tb) return NULL;
    Value* r = (Value*)calloc(1, sizeof(Value));
    if (!r) return NULL;
    if (ta == NVInt_Type && tb == NVInt_Type) {
        create_int(r, ((NVInt*)a->obj)->value + ((NVInt*)b->obj)->value); return r;
    }
    if (ta == NVFloat_Type && tb == NVFloat_Type) {
        create_float(r, ((NVFloat*)a->obj)->value + ((NVFloat*)b->obj)->value); return r;
    }
    if ((ta == NVInt_Type || ta == NVFloat_Type) && (tb == NVInt_Type || tb == NVFloat_Type)) {
        double va = ta == NVFloat_Type ? ((NVFloat*)a->obj)->value : (double)((NVInt*)a->obj)->value;
        double vb = tb == NVFloat_Type ? ((NVFloat*)b->obj)->value : (double)((NVInt*)b->obj)->value;
        create_float(r, va + vb); return r;
    }
    free(r); return NULL;
}

static Value* builtin_subtract(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return NULL;
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (!ta || !tb) return NULL;
    Value* r = (Value*)calloc(1, sizeof(Value));
    if (!r) return NULL;
    if (ta == NVInt_Type && tb == NVInt_Type) {
        create_int(r, ((NVInt*)a->obj)->value - ((NVInt*)b->obj)->value); return r;
    }
    if (ta == NVFloat_Type && tb == NVFloat_Type) {
        create_float(r, ((NVFloat*)a->obj)->value - ((NVFloat*)b->obj)->value); return r;
    }
    if ((ta == NVInt_Type || ta == NVFloat_Type) && (tb == NVInt_Type || tb == NVFloat_Type)) {
        double va = ta == NVFloat_Type ? ((NVFloat*)a->obj)->value : (double)((NVInt*)a->obj)->value;
        double vb = tb == NVFloat_Type ? ((NVFloat*)b->obj)->value : (double)((NVInt*)b->obj)->value;
        create_float(r, va - vb); return r;
    }
    free(r); return NULL;
}

static Value* builtin_multiply(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return NULL;
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (!ta || !tb) return NULL;
    Value* r = (Value*)calloc(1, sizeof(Value));
    if (!r) return NULL;
    if (ta == NVInt_Type && tb == NVInt_Type) {
        create_int(r, ((NVInt*)a->obj)->value * ((NVInt*)b->obj)->value); return r;
    }
    if (ta == NVFloat_Type && tb == NVFloat_Type) {
        create_float(r, ((NVFloat*)a->obj)->value * ((NVFloat*)b->obj)->value); return r;
    }
    if ((ta == NVInt_Type || ta == NVFloat_Type) && (tb == NVInt_Type || tb == NVFloat_Type)) {
        double va = ta == NVFloat_Type ? ((NVFloat*)a->obj)->value : (double)((NVInt*)a->obj)->value;
        double vb = tb == NVFloat_Type ? ((NVFloat*)b->obj)->value : (double)((NVInt*)b->obj)->value;
        create_float(r, va * vb); return r;
    }
    free(r); return NULL;
}

static Value* builtin_divide(Value* a, Value* b) {
    if (!a || !b || !a->obj || !b->obj) return NULL;
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (!ta || !tb) return NULL;
    Value* r = (Value*)calloc(1, sizeof(Value));
    if (!r) return NULL;
    double va = ta == NVFloat_Type ? ((NVFloat*)a->obj)->value : (double)((NVInt*)a->obj)->value;
    double vb = tb == NVFloat_Type ? ((NVFloat*)b->obj)->value : (double)((NVInt*)b->obj)->value;
    if (vb == 0.0) { free(r); return NULL; }
    create_float(r, va / vb);
    return r;
}

static void copy_and_free(Value* out, Value* result) {
    if (result) { *out = *result; free(result); } else { out->obj = NULL; }
}

// ── Public arithmetic API ──────────────────────────────────────────────────

void nv_value_add(Value* out, Value* a, Value* b) { if (out) copy_and_free(out, builtin_add(a, b)); }
void nv_value_sub(Value* out, Value* a, Value* b) { if (out) copy_and_free(out, builtin_subtract(a, b)); }
void nv_value_mul(Value* out, Value* a, Value* b) { if (out) copy_and_free(out, builtin_multiply(a, b)); }
void nv_value_div(Value* out, Value* a, Value* b) { if (out) copy_and_free(out, builtin_divide(a, b)); }

void nv_value_mod(Value* out, Value* a, Value* b) {
    if (!out || !a || !b || !a->obj || !b->obj) { if (out) out->obj = NULL; return; }
    NvTypeObject* ta = a->obj->ob_type;
    NvTypeObject* tb = b->obj->ob_type;
    if (ta == NVInt_Type && tb == NVInt_Type) {
        int32_t vb = ((NVInt*)b->obj)->value;
        if (vb == 0) { out->obj = NULL; return; }
        create_int(out, ((NVInt*)a->obj)->value % vb);
    } else {
        double va = ta == NVFloat_Type ? ((NVFloat*)a->obj)->value : (double)((NVInt*)a->obj)->value;
        double vb = tb == NVFloat_Type ? ((NVFloat*)b->obj)->value : (double)((NVInt*)b->obj)->value;
        if (vb == 0.0) { out->obj = NULL; return; }
        create_float(out, fmod(va, vb));
    }
}

// ── Operator overloading ───────────────────────────────────────────────────

int has_add_method(NvTypeObject* type) {
    if (!type) return 0;
    return type == NVStr_Type || type == NVInt_Type || type == NVFloat_Type;
}

int binary_add(Value* result, Value* lhs, Value* rhs) {
    if (!result || !lhs || !rhs || !lhs->obj || !rhs->obj) return 0;
    memset(result, 0, sizeof(Value));
    NvTypeObject* lt = lhs->obj->ob_type;
    NvTypeObject* rt = rhs->obj->ob_type;
    if (!lt || !rt) return 0;

    // String concatenation
    if (lt == NVStr_Type || rt == NVStr_Type) {
        Value ls = {0}, rs = {0};
        nv_str_convert(&ls, lhs);
        nv_str_convert(&rs, rhs);
        const char* ls_s = (ls.obj && ls.obj->ob_type == NVStr_Type) ? ((NVStr*)ls.obj)->value : "";
        const char* rs_s = (rs.obj && rs.obj->ob_type == NVStr_Type) ? ((NVStr*)rs.obj)->value : "";
        if (!ls_s) ls_s = "";
        if (!rs_s) rs_s = "";
        size_t len = strlen(ls_s) + strlen(rs_s) + 1;
        char* buf = (char*)malloc(len);
        if (!buf) return 0;
        strcpy(buf, ls_s);
        strcat(buf, rs_s);
        create_str(result, buf);
        free(buf);
        return 1;
    }

    // Numeric add
    if ((lt == NVInt_Type || lt == NVFloat_Type) && (rt == NVInt_Type || rt == NVFloat_Type)) {
        Value* r = builtin_add(lhs, rhs);
        if (r) { *result = *r; free(r); return 1; }
    }

    return 0;
}

// ── Builtin class initialization ───────────────────────────────────────────

static NvTypeObject* make_numeric_class(const char* name, size_t basicsize) {
    TypeBuilder* b = type_builder_new(name);
    if (!b) return NULL;
    NvNumberMethods* nm = (NvNumberMethods*)calloc(1, sizeof(NvNumberMethods));
    if (!nm) return NULL;
    nm->nb_add      = builtin_add;
    nm->nb_subtract = builtin_subtract;
    nm->nb_multiply = builtin_multiply;
    nm->nb_divide   = builtin_divide;
    type_builder_set_number_protocol(b, nm);
    NvTypeObject* t = type_builder_build(b);
    if (t) t->tp_basicsize = basicsize;
    return t;
}

static NvTypeObject* make_sequence_class(const char* name, size_t basicsize) {
    TypeBuilder* b = type_builder_new(name);
    if (!b) return NULL;
    NvTypeObject* t = type_builder_build(b);
    if (t) t->tp_basicsize = basicsize;
    return t;
}

static NvTypeObject* make_mapping_class(const char* name, size_t basicsize) {
    TypeBuilder* b = type_builder_new(name);
    if (!b) return NULL;
    NvTypeObject* t = type_builder_build(b);
    if (t) t->tp_basicsize = basicsize;
    return t;
}

// Exception type globals — defined here, declared extern in nv_runtime.h
NvTypeObject* NVError_Type          = NULL;
NvTypeObject* NVValueError_Type     = NULL;
NvTypeObject* NVTypeError_Type      = NULL;
NvTypeObject* NVRuntimeError_Type   = NULL;
NvTypeObject* NVIndexError_Type     = NULL;
NvTypeObject* NVKeyError_Type       = NULL;
NvTypeObject* NVAttributeError_Type = NULL;
NvTypeObject* NVNameError_Type      = NULL;
NvTypeObject* NVAssertionError_Type = NULL;

void initialize_exception_types(void) {
    NVError_Type = nv_type_new("Error", NULL, 0);
    NvTypeObject* bases[] = { NVError_Type };
    NVValueError_Type     = nv_type_new("ValueError",     bases, 1);
    NVTypeError_Type      = nv_type_new("TypeError",      bases, 1);
    NVRuntimeError_Type   = nv_type_new("RuntimeError",   bases, 1);
    NVIndexError_Type     = nv_type_new("IndexError",     bases, 1);
    NVKeyError_Type       = nv_type_new("KeyError",       bases, 1);
    NVAttributeError_Type = nv_type_new("AttributeError", bases, 1);
    NVNameError_Type      = nv_type_new("NameError",      bases, 1);
    NVAssertionError_Type = nv_type_new("AssertionError", bases, 1);
}

void initialize_builtin_classes(void) {
    NVInt_Type    = make_numeric_class("int",   sizeof(NVInt));
    NVFloat_Type  = make_numeric_class("float", sizeof(NVFloat));
    NVBool_Type   = make_numeric_class("bool",  sizeof(NVBool));
    NVChar_Type   = make_numeric_class("char",  sizeof(NVChar));

    NVStr_Type    = make_sequence_class("str",    sizeof(NVStr));
    NVArray_Type  = make_sequence_class("array",  sizeof(NVArray));
    NVVector_Type = make_sequence_class("vector", sizeof(NVVector));
    NVTuple_Type  = make_sequence_class("tuple",  sizeof(NVTuple));

    NVMap_Type    = make_mapping_class("map", sizeof(NVMap));

    nv_tensor_init_type();

    NVObject_Type = nv_type_new("object", NULL, 0);
    NVType_Type   = nv_type_new("type",   NULL, 0);
    if (NVType_Type) {
        NVType_Type->ob_base.ob_type = NVType_Type;
        NVType_Type->tp_metaclass    = NVType_Type;
    }
    if (NVObject_Type && NVType_Type) {
        NVObject_Type->ob_base.ob_type = NVType_Type;
        NVObject_Type->tp_metaclass    = NVType_Type;
    }

    initialize_exception_types();
    initialize_option_result_types();
}
