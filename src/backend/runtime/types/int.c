#include "backend/runtime/nv_runtime.h"
#include <stdio.h>

void create_int(Value* out, int32_t v) {
    if (!out) {
        fputs("FATAL: create_int called with NULL pointer\n", stderr);
        return;
    }
    out->type = TAG_INT;
    out->value = v;
    out->prototype = NULL;
    out->type_info = NULL;
    out->flags = 0;
}
