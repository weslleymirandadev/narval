#include "backend/runtime/nv_runtime.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    NvObject ob_base;
    void*    function_ptr;
    Value**  captured_vars;  // Array of captured Value slots
    int      capture_count;
} NvClosure;

static NvTypeObject* NVClosure_Type = NULL;

static void init_closure_type_once(void) {
    if (NVClosure_Type) return;
    NVClosure_Type = (NvTypeObject*)malloc(sizeof(NvTypeObject));
    memset(NVClosure_Type, 0, sizeof(NvTypeObject));
    NVClosure_Type->ob_base.ob_type  = (NvTypeObject*)(intptr_t)NV_CLOSURE_BASE;
    NVClosure_Type->ob_base.ref_count = 1;
    NVClosure_Type->tp_name           = "closure";
    NVClosure_Type->tp_basicsize      = sizeof(NvClosure);
}

void create_closure(Value* result, void* function_ptr) {
    init_closure_type_once();
    NvClosure* c = (NvClosure*)malloc(sizeof(NvClosure));
    c->ob_base.ob_type  = NVClosure_Type;
    c->ob_base.ref_count = 1;
    c->ob_base.flags     = 0;
    c->function_ptr       = function_ptr;
    c->captured_vars      = NULL;
    c->capture_count      = 0;
    result->obj = (NvObject*)c;
}

Value* nv_closure_cell_new(Value* initial) {
    Value* cell = (Value*)malloc(sizeof(Value));
    if (initial) {
        *cell = *initial;
    } else {
        cell->obj = NULL;
    }
    return cell;
}

void create_closure_with_captures(Value* result, void* function_ptr, Value** captured_vars, int capture_count) {
    init_closure_type_once();
    NvClosure* c = (NvClosure*)malloc(sizeof(NvClosure));
    c->ob_base.ob_type  = NVClosure_Type;
    c->ob_base.ref_count = 1;
    c->ob_base.flags     = 0;
    c->function_ptr       = function_ptr;
    
    // Copy captured variables
    if (capture_count > 0 && captured_vars) {
        c->captured_vars = (Value**)malloc(sizeof(Value*) * capture_count);
        c->capture_count = capture_count;
        for (int i = 0; i < capture_count; i++) {
            c->captured_vars[i] = captured_vars[i];
        }
    } else {
        c->captured_vars = NULL;
        c->capture_count = 0;
    }
    
    result->obj = (NvObject*)c;
}

// Closure body signature: Value fn(Value* args, int argc, Value** captures, int capture_count)
// args is a contiguous array of Value structs on the caller's stack.
// captures is an array of captured Value slots from the outer scope.
void call_closure(Value* closure_val, Value* args, int arg_count, Value* result) {
    if (!closure_val || !closure_val->obj) return;
    NvClosure* c = (NvClosure*)closure_val->obj;
    
    // New signature with captures: Value fn(Value* args, int argc, Value** captures, int capture_count)
    typedef Value (*ClosureFn)(Value* args, int argc, Value** captures, int capture_count);
    ClosureFn fn = (ClosureFn)c->function_ptr;
    *result = fn(args, arg_count, c->captured_vars, c->capture_count);
}
