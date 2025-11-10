#include "backend/runtime/rph_runtime.h"

void create_bool(Value* out, int b) {
    out->type = TAG_BOOL;
    out->value = b ? 1 : 0;
    out->prototype = NULL;
}