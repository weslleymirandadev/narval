// json.c — a JSON reader and writer for the language-level `json` global
// (`json.parse` / `json.parseString` / `json.stringify` / `json.dump`).
//
// The checker has always declared `json` as a global with those four methods, but
// nothing was bound to it at run time: every call compiled with "no value bound for
// 'json' here" and evaluated to 0. These are the functions behind them.
//
// The reader produces the values the literals do: an object is a map (string keys),
// an array is a vector (heterogeneous, like `[...]`), numbers are int when they have
// no fraction or exponent, and true/false/null map to bool and None. The writer emits
// compact JSON with ", " / ": " separators, the same shape `@derive(json)` uses.
#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// nv_make_none lives in nir_bridge.c (same library).
NvObject* nv_make_none(void);

// ── Reading ─────────────────────────────────────────────────────────────────

typedef struct {
    const char* p;
    int ok;
} JParser;

static void jp_ws(JParser* j) {
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') j->p++;
}

// A JSON string literal, returned as a malloc'd C string (the caller frees it).
static char* jp_string_raw(JParser* j) {
    if (*j->p != '"') { j->ok = 0; return NULL; }
    j->p++;

    size_t cap = 32, n = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) { j->ok = 0; return NULL; }
    buf[0] = '\0';

    while (*j->p && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\') {
            char e = *j->p++;
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    unsigned code = 0;
                    int digits = 0;
                    while (digits < 4 && *j->p) {
                        char h = *j->p++;
                        code <<= 4;
                        if (h >= '0' && h <= '9')      code |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
                        else { j->ok = 0; break; }
                        digits++;
                    }
                    char enc[4];
                    int en = 0;
                    if (code < 0x80) {
                        enc[en++] = (char)code;
                    } else if (code < 0x800) {
                        enc[en++] = (char)(0xC0 | (code >> 6));
                        enc[en++] = (char)(0x80 | (code & 0x3F));
                    } else {
                        enc[en++] = (char)(0xE0 | (code >> 12));
                        enc[en++] = (char)(0x80 | ((code >> 6) & 0x3F));
                        enc[en++] = (char)(0x80 | (code & 0x3F));
                    }
                    for (int k = 0; k < en; ++k) {
                        if (n + 2 > cap) {
                            cap *= 2;
                            buf = (char*)realloc(buf, cap);
                            if (!buf) { j->ok = 0; return NULL; }
                        }
                        buf[n++] = enc[k];
                    }
                    continue;
                }
                default: c = e; break;
            }
        }
        if (n + 2 > cap) {
            cap *= 2;
            buf = (char*)realloc(buf, cap);
            if (!buf) { j->ok = 0; return NULL; }
        }
        buf[n++] = c;
    }

    if (*j->p != '"') j->ok = 0;
    else j->p++;
    buf[n] = '\0';
    return buf;
}

static Value jp_value(JParser* j);

static Value jp_object(JParser* j) {
    Value out = {NULL};
    create_map(&out);
    j->p++;                       // '{'
    jp_ws(j);
    if (*j->p == '}') { j->p++; return out; }

    while (*j->p && j->ok) {
        jp_ws(j);
        char* key = jp_string_raw(j);
        if (!j->ok) { free(key); break; }
        jp_ws(j);
        if (*j->p != ':') { j->ok = 0; free(key); break; }
        j->p++;
        Value v = jp_value(j);
        if (!j->ok) { free(key); break; }
        nv_object_set_field(&out, key ? key : "", &v);
        free(key);
        jp_ws(j);
        if (*j->p == ',') { j->p++; continue; }
        if (*j->p == '}') { j->p++; break; }
        j->ok = 0;
    }
    return out;
}

static Value jp_array(JParser* j) {
    Value out = {NULL};
    create_vector(&out, 4);
    j->p++;                       // '['
    jp_ws(j);
    if (*j->p == ']') { j->p++; return out; }

    while (*j->p && j->ok) {
        Value v = jp_value(j);
        if (!j->ok) break;
        Value ignored = {NULL};
        vector_push_method(&ignored, &out, &v);
        jp_ws(j);
        if (*j->p == ',') { j->p++; continue; }
        if (*j->p == ']') { j->p++; break; }
        j->ok = 0;
    }
    return out;
}

static Value jp_value(JParser* j) {
    Value out = {NULL};
    jp_ws(j);
    switch (*j->p) {
        case '{': return jp_object(j);
        case '[': return jp_array(j);
        case '"': {
            char* s = jp_string_raw(j);
            create_str(&out, s ? s : "");
            free(s);
            return out;
        }
        case 't':
            if (strncmp(j->p, "true", 4) == 0) { j->p += 4; create_bool(&out, 1); return out; }
            j->ok = 0; return out;
        case 'f':
            if (strncmp(j->p, "false", 5) == 0) { j->p += 5; create_bool(&out, 0); return out; }
            j->ok = 0; return out;
        case 'n':
            if (strncmp(j->p, "null", 4) == 0) { j->p += 4; out.obj = nv_make_none(); return out; }
            j->ok = 0; return out;
        default: break;
    }

    char* end = NULL;
    double d = strtod(j->p, &end);
    if (end == j->p) { j->ok = 0; return out; }
    int is_int = 1;
    for (const char* q = j->p; q < end; ++q)
        if (*q == '.' || *q == 'e' || *q == 'E') { is_int = 0; break; }
    if (is_int) create_int(&out, (int64_t)d);
    else        create_float(&out, d);
    j->p = end;
    return out;
}

// json.parse(text) / json.parseString(text)
NvObject* nv_json_parse(NvObject* text) {
    if (!text || text->ob_type != NVStr_Type) {
        nv_raise_type_error("json.parse expects a JSON string");
        return NULL;
    }
    const char* s = ((NVStr*)text)->value;
    if (!s) { nv_raise_value_error("json.parse: empty input"); return NULL; }

    JParser j = { s, 1 };
    Value out = jp_value(&j);
    jp_ws(&j);
    if (!j.ok || *j.p != '\0') {
        nv_raise_value_error("json.parse: input is not valid JSON");
        return NULL;
    }
    return out.obj;
}

// ── Writing ─────────────────────────────────────────────────────────────────

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} JBuf;

static void jb_put(JBuf* b, const char* s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < b->len + n + 1) cap *= 2;
        b->data = (char*)realloc(b->data, cap);
        if (!b->data) return;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void jb_puts(JBuf* b, const char* s) { jb_put(b, s, strlen(s)); }
static void jb_putc(JBuf* b, char c)        { jb_put(b, &c, 1); }

static void jb_printf(JBuf* b, const char* fmt, ...) {
    char tmp[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) jb_put(b, tmp, (size_t)(n < (int)sizeof(tmp) ? n : (int)sizeof(tmp) - 1));
}

static void json_write_value(JBuf*, const Value*);

static void json_write_str(const char* s, JBuf* b) {
    jb_putc(b, '"');
    for (const char* q = s; q && *q; ++q) {
        switch (*q) {
            case '"':  jb_puts(b, "\\\""); break;
            case '\\': jb_puts(b, "\\\\"); break;
            case '\n': jb_puts(b, "\\n");  break;
            case '\t': jb_puts(b, "\\t");  break;
            case '\r': jb_puts(b, "\\r");  break;
            case '\b': jb_puts(b, "\\b");  break;
            case '\f': jb_puts(b, "\\f");  break;
            default:   jb_putc(b, *q);     break;
        }
    }
    jb_putc(b, '"');
}

static void json_write_value(JBuf* b, const Value* v) {
    if (!v || !v->obj) { jb_puts(b, "null"); return; }
    NvObject* o = v->obj;

    if (o->ob_type == NVInt_Type)   { jb_printf(b, "%lld", (long long)((NVInt*)o)->value); return; }
    if (o->ob_type == NVFloat_Type) { jb_printf(b, "%.17g", ((NVFloat*)o)->value); return; }
    if (o->ob_type == NVBool_Type)  { jb_puts(b, ((NVBool*)o)->value ? "true" : "false"); return; }
    if (o->ob_type == NVStr_Type)   { json_write_str(((NVStr*)o)->value, b); return; }
    if (o->ob_type == NVOptionNone_Type) { jb_puts(b, "null"); return; }
    if (o->ob_type == NVChar_Type)  { char c[2] = { ((NVChar*)o)->value, '\0' }; json_write_str(c, b); return; }

    if (o->ob_type == NVVector_Type || o->ob_type == NVArray_Type) {
        // Both structs carry elements/size in the same order; the vector is a list and
        // the array a fixed sequence, and JSON writes both as an array.
        NVArray* a = (NVArray*)o;
        jb_putc(b, '[');
        for (int i = 0; i < a->size; ++i) {
            if (i) jb_puts(b, ", ");
            json_write_value(b, &a->elements[i]);
        }
        jb_putc(b, ']');
        return;
    }

    if (o->ob_type == NVMap_Type) {
        NVMap* m = (NVMap*)o;
        jb_putc(b, '{');
        for (int i = 0; i < m->size; ++i) {
            if (i) jb_puts(b, ", ");
            json_write_str(m->keys[i] ? m->keys[i] : "", b);
            jb_puts(b, ": ");
            json_write_value(b, &m->values[i]);
        }
        jb_putc(b, '}');
        return;
    }

    jb_puts(b, "null");
}

static char* json_to_text(NvObject* v) {
    JBuf b = { NULL, 0, 0 };
    jb_puts(&b, "");                        // allocate the buffer
    Value val = { v };
    json_write_value(&b, &val);
    return b.data ? b.data : strdup("null");
}

// json.stringify(value)
NvObject* nv_json_stringify(NvObject* v) {
    char* text = json_to_text(v);
    Value out = {NULL};
    create_str(&out, text ? text : "null");
    free(text);
    return out.obj;
}

// json.dump(value, path) — stringify and write it to a file. Returns the number of
// bytes written, or -1 when the file cannot be opened.
NvObject* nv_json_dump(NvObject* v, NvObject* path) {
    if (!path || path->ob_type != NVStr_Type) {
        nv_raise_type_error("json.dump expects a file path as its second argument");
        return NULL;
    }
    const char* p = ((NVStr*)path)->value;
    FILE* f = p ? fopen(p, "wb") : NULL;
    if (!f) {
        Value out = {NULL};
        create_int(&out, -1);
        return out.obj;
    }
    char* text = json_to_text(v);
    size_t written = text ? fwrite(text, 1, strlen(text), f) : 0;
    free(text);
    fclose(f);

    Value out = {NULL};
    create_int(&out, (int64_t)written);
    return out.obj;
}
