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

// Narval closures compile their bodies as ordinary defs — individual
// !llvm.ptr parameters (user params first, captured values appended). This
// dispatcher calls such a function with the already-unboxed argument values
// (NvObject*) via an arity switch. NOTE: calling through a function pointer
// cast to a different arity is technically UB, but on the SysV ABI pointers
// are passed in registers and this is the standard interpreter trick; keep the
// total arity (user args + captures) at 8 or less.
typedef NvObject* (*Fn0)(void);
typedef NvObject* (*Fn1)(NvObject*);
typedef NvObject* (*Fn2)(NvObject*, NvObject*);
typedef NvObject* (*Fn3)(NvObject*, NvObject*, NvObject*);
typedef NvObject* (*Fn4)(NvObject*, NvObject*, NvObject*, NvObject*);
typedef NvObject* (*Fn5)(NvObject*, NvObject*, NvObject*, NvObject*, NvObject*);
typedef NvObject* (*Fn6)(NvObject*, NvObject*, NvObject*, NvObject*, NvObject*, NvObject*);
typedef NvObject* (*Fn7)(NvObject*, NvObject*, NvObject*, NvObject*, NvObject*, NvObject*, NvObject*);
typedef NvObject* (*Fn8)(NvObject*, NvObject*, NvObject*, NvObject*, NvObject*, NvObject*, NvObject*, NvObject*);

void call_closure_indirect(Value* closure_val, NvObject** args, int arg_count,
                           Value* result) {
    if (!closure_val || !closure_val->obj || !result) {
        if (result) result->obj = NULL;
        return;
    }
    NvClosure* c = (NvClosure*)closure_val->obj;
    if (!c->ob_base.ob_type->tp_name ||
        strcmp(c->ob_base.ob_type->tp_name, "closure") != 0) {
        result->obj = NULL;
        return;
    }
    if (!c->function_ptr) {
        result->obj = NULL;
        return;
    }
    // Read captured cells (Value** array of cells holding NvObject*).
    int ncap = c->capture_count;
    NvObject* cap_vals[8];
    for (int i = 0; i < ncap && i < 8; ++i)
        cap_vals[i] = c->captured_vars[i] ? c->captured_vars[i]->obj : NULL;

    int total = arg_count + ncap;
    NvObject* a[8];
    for (int i = 0; i < arg_count && i < 8; ++i) a[i] = args[i];
    for (int i = 0; i < ncap && i < 8; ++i) a[arg_count + i] = cap_vals[i];

    NvObject* ret = NULL;
    switch (total) {
        case 0: ret = ((Fn0)c->function_ptr)(); break;
        case 1: ret = ((Fn1)c->function_ptr)(a[0]); break;
        case 2: ret = ((Fn2)c->function_ptr)(a[0], a[1]); break;
        case 3: ret = ((Fn3)c->function_ptr)(a[0], a[1], a[2]); break;
        case 4: ret = ((Fn4)c->function_ptr)(a[0], a[1], a[2], a[3]); break;
        case 5: ret = ((Fn5)c->function_ptr)(a[0], a[1], a[2], a[3], a[4]); break;
        case 6: ret = ((Fn6)c->function_ptr)(a[0], a[1], a[2], a[3], a[4], a[5]); break;
        case 7: ret = ((Fn7)c->function_ptr)(a[0], a[1], a[2], a[3], a[4], a[5], a[6]); break;
        default: ret = ((Fn8)c->function_ptr)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]); break;
    }
    result->obj = ret;
}
