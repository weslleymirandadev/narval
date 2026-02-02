#include "backend/runtime/nv_runtime.h"
#include <stdio.h>

void create_bool(Value* out, int b) {
    if (!out) {
        fputs("FATAL: create_bool called with NULL pointer\n", stderr);
        return;
    }
    out->type = TAG_BOOL;
    out->value = b ? 1 : 0;
    out->prototype = NULL;
    out->type_info = NULL;
    out->flags = 0;
}
