#include "backend/runtime/rph_runtime.h"

void create_json(Value* out, Value v) {
    *out = v;
    out->type = TAG_JSON;
}