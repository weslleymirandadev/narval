#include "backend/runtime/nv_runtime.h"
#include <stdio.h>
#include <string.h>

void create_float(Value* out, double v) {
    if (!out) {
        fputs("FATAL: create_float called with NULL pointer\n", stderr);
        return;
    }
    out->type = TAG_FLOAT;
    // Armazenar double em value (int64_t)
    memcpy(&out->value, &v, sizeof(double));
    out->prototype = NULL;
    out->type_info = NULL;
    out->flags = 0;
}
