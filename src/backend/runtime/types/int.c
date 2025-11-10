#include "backend/runtime/rph_runtime.h"

void create_int(Value* out, int32_t v) {
    out->type = TAG_INT;
    out->value = v;
    out->prototype = NULL;
}