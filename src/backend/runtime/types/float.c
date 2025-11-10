#include "backend/runtime/rph_runtime.h"

void create_float(Value* out, double v) {
    out->type = TAG_FLOAT;
    int64_t bits = 0;
    memcpy(&bits, &v, sizeof(double));
    out->value = bits;
    out->prototype = NULL;
}