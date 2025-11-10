#include "backend/runtime/rph_runtime.h"

void create_record(Value* out, int field_count, const char** field_names) {
    Record* r = (Record*)malloc(sizeof(Record));
    if (!r) {
        fprintf(stderr, "FATAL: malloc failed in create_record\n");
        exit(1);
    }
    r->field_count = field_count;
    r->field_names = (char**)malloc(sizeof(char*) * field_count);
    r->fields = (Value*)malloc(sizeof(Value) * field_count);
    if (!r->field_names || !r->fields) {
        free(r->field_names); free(r->fields); free(r);
        fprintf(stderr, "FATAL: malloc failed in create_record buffers\n");
        exit(1);
    }

    for (int i = 0; i < field_count; ++i) {
        r->field_names[i] = strdup(field_names[i]);
        r->fields[i] = (Value){0};
    }

    out->type = TAG_RECORD;
    out->value = (int64_t)(intptr_t)r;
    out->prototype = NULL;
}

Value record_get_impl(Value* self, const char* field) {
    if (!self || self->type != TAG_RECORD || !field) return (Value){0};
    Record* r = (Record*)(intptr_t)self->value;
    for (int i = 0; i < r->field_count; ++i) {
        if (strcmp(r->field_names[i], field) == 0) return r->fields[i];
    }
    return (Value){0};
}

void record_set_impl(Value* self, const char* field, Value v) {
    if (!self || self->type != TAG_RECORD || !field) return;
    Record* r = (Record*)(intptr_t)self->value;
    for (int i = 0; i < r->field_count; ++i) {
        if (strcmp(r->field_names[i], field) == 0) {
            r->fields[i] = v;
            return;
        }
    }
}