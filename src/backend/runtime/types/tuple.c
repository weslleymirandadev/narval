#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <stdlib.h>

void create_tuple(Value* out, int field_count) {
    Tuple* t = (Tuple*)malloc(sizeof(Tuple));
    if (!t) {
        fprintf(stderr, "FATAL: malloc failed in create_tuple\n");
        exit(1);
    }
    t->field_count = field_count;
    t->fields = (Value*)calloc(field_count, sizeof(Value));
    if (!t->fields && field_count > 0) {
        free(t);
        fprintf(stderr, "FATAL: calloc failed in create_tuple\n");
        exit(1);
    }

    out->type = TAG_TUPLE;
    out->value = (int64_t)(intptr_t)t;
    out->prototype = NULL;
    out->type_info = NULL;
    out->flags = 0;
}

Value tuple_get_impl(Value* self, int index) {
    if (!self || self->type != TAG_TUPLE || index < 0) return (Value){0};
    Tuple* t = (Tuple*)(intptr_t)self->value;
    if (index >= t->field_count) return (Value){0};
    return t->fields[index];
}

void tuple_set_impl(Value* self, int index, const Value* v) {
    if (!self || self->type != TAG_TUPLE || index < 0) return;
    Tuple* t = (Tuple*)(intptr_t)self->value;
    if (index >= t->field_count) return;
    if (!v) { t->fields[index] = (Value){0}; return; }
    t->fields[index] = *v;
}