#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>

NvTypeObject* NVFuture_Type = NULL;

extern NvTypeObject* nv_type_new(const char* name, NvTypeObject** bases, int base_count);

static void initialize_future_type(void) {
    if (NVFuture_Type) return;
    NVFuture_Type = nv_type_new("Future", NULL, 0);
    if (NVFuture_Type) {
        NVFuture_Type->tp_basicsize = sizeof(NVFuture);
        NVFuture_Type->tp_dealloc = (void (*)(struct NvObject*))free;
    }
}

void create_future_resolved(Value* out, Value* val) {
    if (!out) return;
    initialize_future_type();
    if (!NVFuture_Type) {
        out->obj = NULL;
        return;
    }

    NVFuture* fut = (NVFuture*)calloc(1, sizeof(NVFuture));
    if (!fut) {
        out->obj = NULL;
        return;
    }

    fut->ob_base.ob_type = NVFuture_Type;
    fut->ob_base.ref_count = 1;
    fut->ob_base.flags = 0;
    fut->fiber = NULL;
    fut->resolved = val ? *val : (Value){NULL};
    fut->is_resolved = 1;
    out->obj = (NvObject*)fut;
}

void nv_await(Value* out, Value* future) {
    if (!out) return;
    if (!future || !future->obj || future->obj->ob_type != NVFuture_Type) {
        *out = future ? *future : (Value){NULL};
        return;
    }

    NVFuture* fut = (NVFuture*)future->obj;
    *out = fut->is_resolved ? fut->resolved : (Value){NULL};
}
