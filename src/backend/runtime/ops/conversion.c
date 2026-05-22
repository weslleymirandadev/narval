// ops/conversion.c — Type conversions and string formatting.
// Merges: type_conversion.c + string_concat.c

#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// ── String helpers ─────────────────────────────────────────────────────────

char* string_concat(char* str1, char* str2) {
    if (!str1) str1 = "";
    if (!str2) str2 = "";
    size_t len = strlen(str1) + strlen(str2) + 1;
    char* r = (char*)malloc(len);
    if (!r) return NULL;
    strcpy(r, str1);
    strcat(r, str2);
    return r;
}

static void i32_to_cstr(int32_t value, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (out_size == 1) { out[0] = '\0'; return; }
    if (value == 0) { out[0] = '0'; out[1] = '\0'; return; }
    char tmp[16]; int i = 0;
    uint32_t v = (value < 0) ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    while (v > 0 && i < (int)sizeof(tmp)) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    size_t pos = 0;
    if (value < 0 && pos < out_size - 1) out[pos++] = '-';
    while (i > 0 && pos < out_size - 1) out[pos++] = tmp[--i];
    out[pos] = '\0';
}

void int_to_string(Value* out, int32_t value) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    char buf[32]; i32_to_cstr(value, buf, sizeof(buf));
    create_str(out, buf);
}

void float_to_string(Value* out, double value) {
    if (!out) return;
    memset(out, 0, sizeof(Value));
    char buf[64]; snprintf(buf, sizeof(buf), "%g", value);
    create_str(out, buf);
}

// ── str() ─────────────────────────────────────────────────────────────────

void nv_str_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) { create_str(out, "None"); return; }
    if ((uintptr_t)input->obj < 0x1000) { create_str(out, "None"); return; }
    NvTypeObject* type = input->obj->ob_type;
    if (!type || (uintptr_t)type < 0x1000) { create_str(out, "None"); return; }

    if (type == NVInt_Type) {
        char buf[32]; i32_to_cstr(((NVInt*)input->obj)->value, buf, sizeof(buf));
        create_str(out, buf);
    } else if (type == NVFloat_Type) {
        char buf[64]; snprintf(buf, sizeof(buf), "%.15g", ((NVFloat*)input->obj)->value);
        create_str(out, buf);
    } else if (type == NVBool_Type) {
        create_str(out, ((NVBool*)input->obj)->value ? "true" : "false");
    } else if (type == NVChar_Type) {
        char buf[2] = { ((NVChar*)input->obj)->value, '\0' };
        create_str(out, buf);
    } else if (type == NVStr_Type) {
        NVStr* s = (NVStr*)input->obj;
        create_str(out, s->value ? s->value : "");
    } else if (type == NVArray_Type) {
        NVArray* arr = (NVArray*)input->obj;
        size_t buf_sz = 64; char* buf = (char*)malloc(buf_sz);
        if (!buf) { create_str(out, "[]"); return; }
        size_t pos = 0; buf[pos++] = '{';
        for (int i = 0; i < arr->size; i++) {
            Value es = {0}; nv_str_convert(&es, &arr->elements[i]);
            const char* sv = (es.obj && es.obj->ob_type == NVStr_Type && ((NVStr*)es.obj)->value) ? ((NVStr*)es.obj)->value : "";
            size_t sl = strlen(sv);
            if (pos + sl + 4 > buf_sz) {
                buf_sz = (pos + sl + 4) * 2;
                char* nb = (char*)realloc(buf, buf_sz);
                if (!nb) { free(buf); create_str(out, "{...}"); return; }
                buf = nb;
            }
            memcpy(buf + pos, sv, sl); pos += sl;
            if (i < arr->size - 1) { buf[pos++] = ','; buf[pos++] = ' '; }
        }
        buf[pos++] = '}'; buf[pos] = '\0';
        create_str(out, buf); free(buf);
    } else if (type == NVVector_Type) {
        NVVector* vec = (NVVector*)input->obj;
        size_t buf_sz = 64; char* buf = (char*)malloc(buf_sz);
        if (!buf) { create_str(out, "[]"); return; }
        size_t pos = 0; buf[pos++] = '[';
        for (int i = 0; i < vec->size; i++) {
            Value es = {0}; nv_str_convert(&es, &vec->elements[i]);
            const char* sv = (es.obj && es.obj->ob_type == NVStr_Type && ((NVStr*)es.obj)->value) ? ((NVStr*)es.obj)->value : "";
            size_t sl = strlen(sv);
            if (pos + sl + 4 > buf_sz) {
                buf_sz = (pos + sl + 4) * 2;
                char* nb = (char*)realloc(buf, buf_sz);
                if (!nb) { free(buf); create_str(out, "[...]"); return; }
                buf = nb;
            }
            memcpy(buf + pos, sv, sl); pos += sl;
            if (i < vec->size - 1) { buf[pos++] = ','; buf[pos++] = ' '; }
        }
        buf[pos++] = ']'; buf[pos] = '\0';
        create_str(out, buf); free(buf);
    } else if (type == NVMap_Type) {
        NVMap* map = (NVMap*)input->obj;
        Value cn = {0};
        nv_object_get_field(&cn, input, "__class_name__");
        if (cn.obj && cn.obj->ob_type == NVStr_Type) {
            char buf[256]; const char* n = ((NVStr*)cn.obj)->value ? ((NVStr*)cn.obj)->value : "object";
            snprintf(buf, sizeof(buf), "<object:%s>", n); create_str(out, buf);
        } else if (map->str_method) {
            *out = map->str_method(input);
        } else {
            create_str(out, "<map object>");
        }
    } else if (type == NVOptionNone_Type) {
        create_str(out, "None");
    } else if (type == NVOptionSome_Type) {
        nv_str_convert(out, &((NVOptionSome*)input->obj)->inner);
    } else if (type == NVResultOk_Type) {
        nv_str_convert(out, &((NVResultOk*)input->obj)->inner);
    } else if (type == NVResultErr_Type) {
        nv_str_convert(out, &((NVResultErr*)input->obj)->inner);
    } else {
        const char* tname = type->tp_name ? type->tp_name : "object";
        char buf[256]; snprintf(buf, sizeof(buf), "<object:%s>", tname);
        create_str(out, buf);
    }
}

// ── int() ─────────────────────────────────────────────────────────────────

void nv_int_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) { create_int(out, 0); return; }
    NvTypeObject* t = input->obj->ob_type;
    if (t == NVInt_Type)   { create_int(out, ((NVInt*)input->obj)->value); return; }
    if (t == NVFloat_Type) { create_int(out, (int32_t)((NVFloat*)input->obj)->value); return; }
    if (t == NVBool_Type)  { create_int(out, ((NVBool*)input->obj)->value ? 1 : 0); return; }
    if (t == NVChar_Type)  { create_int(out, (int32_t)(unsigned char)((NVChar*)input->obj)->value); return; }
    if (t == NVStr_Type) {
        const char* s = ((NVStr*)input->obj)->value;
        if (s) { char* e; long v = strtol(s, &e, 10); if (*e == '\0' && e != s) { create_int(out, (int32_t)v); return; } }
        nv_raise_value_error("invalid literal for int()");
        return;
    }
    nv_raise_type_error("int() argument must be a str or number");
}

// ── char() ────────────────────────────────────────────────────────────────

void nv_char_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) { create_char(out, '\0'); return; }
    NvTypeObject* t = input->obj->ob_type;
    if (t == NVChar_Type)  { create_char(out, ((NVChar*)input->obj)->value); return; }
    if (t == NVInt_Type)   { create_char(out, (char)((NVInt*)input->obj)->value); return; }
    if (t == NVBool_Type)  { create_char(out, ((NVBool*)input->obj)->value ? 1 : 0); return; }
    if (t == NVFloat_Type) { create_char(out, (char)((NVFloat*)input->obj)->value); return; }
    if (t == NVStr_Type) {
        NVStr* s = (NVStr*)input->obj;
        if (s->value && s->len == 1) { create_char(out, s->value[0]); return; }
    }
    nv_raise_value_error("char() argument must be a one-character str or number");
}

// ── float() ───────────────────────────────────────────────────────────────

void nv_float_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) { create_float(out, 0.0); return; }
    NvTypeObject* t = input->obj->ob_type;
    if (t == NVInt_Type)   { create_float(out, (double)((NVInt*)input->obj)->value); return; }
    if (t == NVFloat_Type) { create_float(out, ((NVFloat*)input->obj)->value); return; }
    if (t == NVBool_Type)  { create_float(out, ((NVBool*)input->obj)->value ? 1.0 : 0.0); return; }
    if (t == NVChar_Type)  { create_float(out, (double)(unsigned char)((NVChar*)input->obj)->value); return; }
    if (t == NVStr_Type) {
        const char* s = ((NVStr*)input->obj)->value;
        if (s) { char* e; double v = strtod(s, &e); if (*e == '\0' && e != s) { create_float(out, v); return; } }
        nv_raise_value_error("invalid literal for float()");
        return;
    }
    nv_raise_type_error("float() argument must be a str or number");
}

// ── bool() ────────────────────────────────────────────────────────────────

void nv_bool_convert(Value* out, Value* input) {
    if (!out || !input || !input->obj) { create_bool(out, 0); return; }
    NvTypeObject* t = input->obj->ob_type;
    if (t == NVInt_Type)    { create_bool(out, ((NVInt*)input->obj)->value != 0); return; }
    if (t == NVFloat_Type)  { create_bool(out, ((NVFloat*)input->obj)->value != 0.0); return; }
    if (t == NVBool_Type)   { create_bool(out, ((NVBool*)input->obj)->value); return; }
    if (t == NVChar_Type)   { create_bool(out, ((NVChar*)input->obj)->value != '\0'); return; }
    if (t == NVStr_Type)    { NVStr* s = (NVStr*)input->obj; create_bool(out, s->value && s->len > 0); return; }
    if (t == NVArray_Type)  { create_bool(out, ((NVArray*)input->obj)->size > 0); return; }
    if (t == NVVector_Type) { create_bool(out, ((NVVector*)input->obj)->size > 0); return; }
    if (t == NVMap_Type)    { create_bool(out, ((NVMap*)input->obj)->size > 0); return; }
    if (t == NVTuple_Type)  { create_bool(out, ((NVTuple*)input->obj)->field_count > 0); return; }
    create_bool(out, 1);
}
