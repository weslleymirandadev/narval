// collections/io.c — I/O: write, read, and tensor print dispatch.

#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void nv_write(Value* v) {
    if (!v || (uintptr_t)v < 0x1000 || !v->obj || (uintptr_t)v->obj < 0x1000) {
        printf("None\n"); return;
    }
    NvTypeObject* type = v->obj->ob_type;
    if (!type || (uintptr_t)type < 0x1000) { printf("<unknown>\n"); return; }

    if (type == NVStr_Type) {
        NVStr* s = (NVStr*)v->obj;
        printf("%s\n", s->value ? s->value : "");
    } else if (type == NVInt_Type) {
        printf("%d\n", ((NVInt*)v->obj)->value);
    } else if (type == NVFloat_Type) {
        printf("%f\n", ((NVFloat*)v->obj)->value);
    } else if (type == NVBool_Type) {
        printf("%s\n", ((NVBool*)v->obj)->value ? "true" : "false");
    } else if (type == NVChar_Type) {
        printf("%c\n", ((NVChar*)v->obj)->value);
    } else if (type == NVArray_Type) {
        NVArray* arr = (NVArray*)v->obj;
        const char* tn = (arr->size > 0 && arr->elements[0].obj && arr->elements[0].obj->ob_type)
            ? arr->elements[0].obj->ob_type->tp_name : "?";
        printf("<%s[%d]>\n", tn, arr->size);
    } else if (type == NVMap_Type) {
        NVMap* map = (NVMap*)v->obj;
        Value cls = {0};
        nv_object_get_field(&cls, v, "__class_name__");
        if (cls.obj && cls.obj->ob_type == NVStr_Type) {
            printf("<object:%s>\n", ((NVStr*)cls.obj)->value ? ((NVStr*)cls.obj)->value : "object");
        } else if (map->str_method) {
            Value tmp = map->str_method(v);
            if (tmp.obj && tmp.obj->ob_type == NVStr_Type)
                printf("%s\n", ((NVStr*)tmp.obj)->value ? ((NVStr*)tmp.obj)->value : "");
            else printf("<map object>\n");
        } else {
            printf("<map object>\n");
        }
    } else if (type == NVTensor_Type) {
        nv_tensor_print(v);
    } else {
        printf("<object:%s>\n", type->tp_name ? type->tp_name : "object");
    }
    fflush(stdout);
}

void nv_write_no_nl(Value* v) {
    if (!v || !v->obj) { printf("None"); return; }
    NvTypeObject* type = v->obj->ob_type;
    if (!type) { printf("<unknown>"); return; }
    if (type == NVStr_Type)   { NVStr* s = (NVStr*)v->obj; if (s->value) printf("%s", s->value); }
    else if (type == NVInt_Type)   printf("%d",  ((NVInt*)v->obj)->value);
    else if (type == NVFloat_Type) printf("%f",  ((NVFloat*)v->obj)->value);
    else if (type == NVBool_Type)  printf("%s",  ((NVBool*)v->obj)->value ? "true" : "false");
    else if (type == NVChar_Type)  printf("%c",  ((NVChar*)v->obj)->value);
    else printf("<object:%s>", type->tp_name ? type->tp_name : "object");
    fflush(stdout);
}

char* nv_read(const char* prompt) {
    if (prompt) { printf("%s", prompt); fflush(stdout); }
    size_t bufsz = 128;
    char* buf = (char*)malloc(bufsz);
    if (!buf) return NULL;
    size_t idx = 0; int ch;
    while ((ch = getchar()) != EOF && ch != '\n') {
        if (idx + 1 >= bufsz) {
            size_t ns = bufsz * 2;
            char* n = (char*)realloc(buf, ns);
            if (!n) { free(buf); return NULL; }
            buf = n; bufsz = ns;
        }
        buf[idx++] = (char)ch;
    }
    if (ch == EOF && idx == 0) { free(buf); return NULL; }
    buf[idx] = '\0';
    return buf;
}
