#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Weak default for base directory; strong definitions in drivers override this
#if defined(__GNUC__)
__attribute__((weak)) const char* nv_base_dir = NULL;
#else
const char* nv_base_dir = NULL;
#endif

static Value json_parse_value(const char** ptr);

static void skip_ws(const char** ptr) {
    while (**ptr && (**ptr == ' ' || **ptr == '\t' || **ptr == '\n' || **ptr == '\r')) (*ptr)++;
}

static Value json_parse_object(const char** ptr) {
    Value obj;
    create_map(&obj);
    (*ptr)++; // skip '{'
    skip_ws(ptr);

    while (**ptr && **ptr != '}') {
        skip_ws(ptr);
        if (**ptr != '"') break;
        (*ptr)++; // skip quote

        // parse key
        const char* start = *ptr;
        while (**ptr && **ptr != '"') (*ptr)++;
        int len = *ptr - start;
        char* key = malloc(len + 1);
        memcpy(key, start, len); key[len] = '\0';
        (*ptr)++; // skip quote
        skip_ws(ptr);
        if (**ptr != ':') { free(key); break; }
        (*ptr)++; // skip :
        skip_ws(ptr);

        Value val = json_parse_value(ptr);
        map_set_method(&obj, key, val);
        free(key);

        skip_ws(ptr);
        if (**ptr == ',') (*ptr)++;
        else if (**ptr != '}') break;
    }
    if (**ptr == '}') (*ptr)++;
    return obj;
}

static Value json_parse_array(const char** ptr) {
    Value arr;
    create_vector(&arr, 0);
    (*ptr)++; // skip '['
    skip_ws(ptr);

    while (**ptr && **ptr != ']') {
        Value val = json_parse_value(ptr);
        Value tmp_out; /* vector_push_method requires an out parameter */
        vector_push_method(&tmp_out, &arr, &val);
        skip_ws(ptr);
        if (**ptr == ',') (*ptr)++;
    }
    if (**ptr == ']') (*ptr)++;
    return arr;
}

static Value json_parse_value(const char** ptr) {
    skip_ws(ptr);
    if (!**ptr) return (Value){0};

    if (**ptr == '{') return json_parse_object(ptr);
    if (**ptr == '[') return json_parse_array(ptr);
    if (**ptr == '"') {
        (*ptr)++; // skip quote
        const char* start = *ptr;
        while (**ptr && **ptr != '"') (*ptr)++;
        int len = *ptr - start;
        char* s = malloc(len + 1);
        memcpy(s, start, len); s[len] = '\0';
        (*ptr)++; // skip quote
        Value str; create_str(&str, s); free(s);
        return str;
    }
    if (strncmp(*ptr, "true", 4) == 0) { *ptr += 4; Value b; create_bool(&b, 1); return b; }
    if (strncmp(*ptr, "false", 5) == 0) { *ptr += 5; Value b; create_bool(&b, 0); return b; }
    if (strncmp(*ptr, "null", 4) == 0) { *ptr += 4; return (Value){0}; }

    // number
    char* end;
    double d = strtod(*ptr, &end);
    if (end > *ptr) {
        *ptr = end;
        Value v;
        if (d == (int64_t)d) { create_int(&v, (int64_t)d); }
        else { create_float(&v, d); }
        return v;
    }

    return (Value){0};
}

Value json_parse(const char* input) {
    const char* ptr = input;
    Value result = json_parse_value(&ptr);
    return result;
}

static int is_absolute_path(const char* p) {
    return p && p[0] == '/';
}

void json_load(Value* out, const char* filename) {
    /* nv_base_dir may be provided by the generated program; weak default here */
    const char* base = nv_base_dir; // no env vars
    char* fullpath = NULL;
    if (filename && !is_absolute_path(filename) && base && base[0]) {
        size_t bl = strlen(base);
        size_t fl = strlen(filename);
        int need_slash = (bl > 0 && base[bl - 1] != '/');
        fullpath = (char*)malloc(bl + need_slash + fl + 1);
        if (!fullpath) { *out = (Value){0}; return; }
        memcpy(fullpath, base, bl);
        if (need_slash) { fullpath[bl] = '/'; bl += 1; }
        memcpy(fullpath + bl, filename, fl);
        fullpath[bl + fl] = '\0';
    }

    const char* try1 = fullpath ? fullpath : filename;
    FILE* f = try1 ? fopen(try1, "rb") : NULL;
    if (!f && fullpath) {
        free(fullpath);
        fullpath = NULL;
        f = filename ? fopen(filename, "rb") : NULL;
    }
    if (!f) {
        *out = (Value){0};
        return;
    }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); if (fullpath) free(fullpath); *out = (Value){0}; return; }
    long len = ftell(f);
    if (len < 0) { fclose(f); if (fullpath) free(fullpath); *out = (Value){0}; return; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); if (fullpath) free(fullpath); *out = (Value){0}; return; }
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); if (fullpath) free(fullpath); *out = (Value){0}; return; }
    size_t rd = fread(buf, 1, (size_t)len, f);
    buf[rd] = '\0';
    fclose(f);
    if (fullpath) { free(fullpath); }

    Value v = json_parse(buf);
    free(buf);
    *out = v;
    return;
}