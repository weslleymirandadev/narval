#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern void* string_prototype;

void create_str(Value* out, const char* s) {
    if (!out) {
        fputs("FATAL: create_str called with NULL pointer\n", stderr);
        return;
    }
    if (!s) s = "";
    size_t len = strlen(s);
    char* data = (char*)malloc(len + 1);
    if (!data) {
        fputs("FATAL: malloc failed in create_str\n", stderr);    
        exit(1);
    }
    memcpy(data, s, len + 1);
    out->type = TAG_STR;
    out->value = (int64_t)(intptr_t)data;
    out->prototype = string_prototype;
    out->type_info = NULL;
    out->flags = 0;
}

void string_to_upper_case(Value* out, Value* self) {
    char* src = (char*)(intptr_t)self->value;
    size_t len = strlen(src);
    char* up = (char*)malloc(len + 1);
    for (size_t i = 0; i < len; ++i) up[i] = toupper(src[i]);
    up[len] = '\0';
    create_str(out, up);
}

void string_replace(Value* out, Value* self, Value* old_val, Value* new_val) {
    if (!self) { *out = (Value){0,0,NULL}; return; }
    char* src = (char*)(intptr_t)self->value;
    char* old_str = old_val ? (char*)(intptr_t)old_val->value : NULL;
    char* new_str = new_val ? (char*)(intptr_t)new_val->value : NULL;

    if (!src || !old_str || !new_str || !old_str[0]) {
        *out = *self;
        return;
    }

    char* pos = strstr(src, old_str);
    if (!pos) {
        *out = *self;
        return;
    }

    ptrdiff_t before = pos - src;
    size_t old_len = strlen(old_str);
    size_t nw_len = strlen(new_str);
    size_t after = strlen(pos + old_len);
    size_t new_len = before + nw_len + after;

    char* result = (char*)malloc(new_len + 1);
    if (!result) {
        *out = *self;
        return;
    }

    memcpy(result, src, before);
    memcpy(result + before, new_str, nw_len);
    memcpy(result + before + nw_len, pos + old_len, after);
    result[new_len] = '\0';

    create_str(out, result);
    free(result);
}

char* string_repeat(const char* s, int n) {
    if (!s || n <= 0) {
        char* r = (char*)malloc(1);
        if (r) r[0] = '\0';
        return r;
    }
    size_t len = strlen(s);
    if (len == 0) {
        char* r = (char*)malloc(1);
        if (r) r[0] = '\0';
        return r;
    }
    size_t total;
    if (__builtin_mul_overflow(len, (size_t)n, &total)) {
        /* Fallback on overflow: return empty */
        char* r = (char*)malloc(1);
        if (r) r[0] = '\0';
        return r;
    }
    char* out = (char*)malloc(total + 1);
    if (!out) {
        return NULL;
    }
    char* p = out;
    for (int i = 0; i < n; ++i) {
        memcpy(p, s, len);
        p += len;
    }
    out[total] = '\0';
    return out;
}

char* string_concat(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char* out = (char*)malloc(la + lb + 1);
    if (!out) return NULL;
    memcpy(out, a, la);
    memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

void string_includes(Value* out, Value* self, Value substr) {
    if (!self || self->type != TAG_STR || substr.type != TAG_STR) {
        create_bool(out, 0);
        return;
    }
    char* src = (char*)(intptr_t)self->value;
    char* sub = (char*)(intptr_t)substr.value;
    if (!src || !sub || sub[0] == '\0') { create_bool(out, 0); return; }
    create_bool(out, strstr(src, sub) != NULL);
}