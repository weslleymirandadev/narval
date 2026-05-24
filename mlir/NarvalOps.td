#ifndef NARVAL_OPS_TD
#define NARVAL_OPS_TD

include "mlir/IR/OpBase.td"
include "mlir/IR/BuiltinTypes.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/CommonAttrConstraints.td"
include "mlir/Interfaces/SideEffectInterfaces.td"
include "mlir/Interfaces/CallInterfaces.td"
include "mlir/Interfaces/ControlFlowInterfaces.td"
include "mlir/Interfaces/InferTypeOpInterface.td"
include "NarvalDialect.td"
include "NarvalTypes.td"
include "NarvalAttrs.td"

class Narval_Op<string mnemonic, list<Trait> traits = []>
    : Op<Narval_Dialect, mnemonic, traits>;


def Narval_AllocOp : Narval_Op<"alloc"> {
    let summary = "Allocate a new Narval value using the runtime";
    let description = [{
        Allocates a new boxed Narval value by calling the appropriate runtime
        constructor (e.g. `nv_alloc_int`, `nv_alloc_object`).

        Example:
            %v = narval.alloc "nv_alloc_int", 1 : !narval.value
    }];
    let arguments = (ins
        StrAttr:$runtime_ctor,  // runtime function name: "nv_alloc_int", etc.
        I32Attr:$type_tag       // NV_INT_BASE=1, NV_FLOAT_BASE=2, etc.
    );
    let results = (outs Narval_ValueType:$result);
    let assemblyFormat = "$runtime_ctor `,` $type_tag attr-dict `:` type($result)";
}

def Narval_MoveOp : Narval_Op<"move", [Pure]> {
    let summary = "Transfer ownership of a Narval value";
    let description = [{
        Marks a transfer of ownership. The source value must not be used after
        this operation. At runtime this is an identity (same pointer); the
        semantic enforcement is done by the ownership analysis pass which inserts
        narval.drop at the last use of the source.
    }];
    let arguments = (ins Narval_ValueType:$source);
    let results = (outs Narval_ValueType:$result);
    let assemblyFormat = "$source attr-dict `:` type($result)";
    let hasCanonicalizer = 1;
}

def Narval_BorrowOp : Narval_Op<"borrow", [Pure]> {
    let summary = "Create an immutable borrow of a Narval value";
    let description = [{
        Creates a shared (immutable) reference to a value. The borrow must not
        outlive the source. Lowers to a raw pointer without copying.
    }];
    let arguments = (ins Narval_ValueType:$source);
    let results = (outs Narval_RefType:$result);
    let assemblyFormat = "$source attr-dict `:` type($result)";
}

def Narval_BorrowMutOp : Narval_Op<"borrow_mut"> {
    let summary = "Create a mutable borrow of a Narval value";
    let description = [{
        Creates an exclusive mutable reference. No other borrows may be live
        simultaneously. Lowers to a raw pointer without copying.
    }];
    let arguments = (ins Narval_ValueType:$source);
    let results = (outs Narval_MutRefType:$result);
    let assemblyFormat = "$source attr-dict `:` type($result)";
}

def Narval_DropOp : Narval_Op<"drop"> {
    let summary = "Release a Narval value and run its destructor";
    let description = [{
        Releases ownership and calls the appropriate runtime destructor or
        nv_free. Normally inserted automatically by the ownership analysis pass
        at the last use point of each value.
    }];
    let arguments = (ins Narval_ValueType:$value);
    let assemblyFormat = "$value attr-dict";
}

// Constant / Literal ops

def Narval_ConstantOp : Narval_Op<"constant", [Pure, ConstantLike]> {
    let summary = "Embed a compile-time constant as a Narval value";
    let description = [{
        Holds a compile-time constant that will be lowered to the appropriate
        runtime value creation call. Used for integer, float, bool, and string
        literals emitted by the codegen.
    }];
    let arguments = (ins AnyAttr:$value);
    let results = (outs AnyType:$result);
    // AnyAttr before ':' is format-ambiguous; use generic MLIR form.
    let hasCustomAssemblyFormat = 0;
    let hasFolder = 1;
}

def Narval_ComptimeConstOp : Narval_Op<"comptime_const", [Pure, ConstantLike]> {
    let summary = "Compile-time constant produced by ComptimeEvaluator";
    let description = [{
        Wraps the result of a comptime expression that has been fully evaluated
        by the ComptimeEvaluator during the checker phase. The value attribute
        holds the evaluated constant; this op is always folded away before
        reaching the final LLVM lowering.
    }];
    let arguments = (ins AnyAttr:$value);
    let results = (outs AnyType:$result);
    // AnyAttr before ':' is format-ambiguous; use generic MLIR form.
    let hasCustomAssemblyFormat = 0;
    let hasFolder = 1;
    let hasCanonicalizer = 1;
}

// Function call ops

def Narval_CallOp : Narval_Op<"call"> {
    let summary = "Call a Narval function";
    let description = [{
        Calls a named Narval function. Lowers to func.call after the Narval
        function lowering pass runs.

        Example:
            %r = narval.call @add(%a, %b) : (!narval.value, !narval.value) -> !narval.value
    }];
    let arguments = (ins
        FlatSymbolRefAttr:$callee,
        Variadic<AnyType>:$operands
    );
    let results = (outs Variadic<AnyType>:$results);
    let assemblyFormat = [{
        $callee `(` $operands `)` attr-dict `:` functional-type($operands, results)
    }];
}

def Narval_CallRuntimeOp : Narval_Op<"call_runtime"> {
    let summary = "Call a Narval C runtime function";
    let description = [{
        Calls a function from the Narval C runtime (e.g., nv_object_get_field,
        nv_add, nv_free). Lowers directly to func.call with the runtime symbol.

        Example:
            %r = narval.call_runtime @nv_object_get_field(%obj, %key)
                     : (!narval.value, !llvm.ptr) -> !narval.value
    }];
    let arguments = (ins
        FlatSymbolRefAttr:$callee,
        Variadic<AnyType>:$operands
    );
    let results = (outs Variadic<AnyType>:$results);
    let assemblyFormat = [{
        $callee `(` $operands `)` attr-dict `:` functional-type($operands, results)
    }];
}

def Narval_ReturnOp : Narval_Op<"return", [Pure, Terminator, ReturnLike]> {
    let summary = "Return from the current Narval function body";
    let description = [{
        Terminates a function region. If the function is fallible, the returned
        value is automatically wrapped in Ok(...) by the lowering pass.
    }];
    let arguments = (ins Variadic<AnyType>:$operands);
    let assemblyFormat = "($operands^ `:` type($operands))? attr-dict";
}

// Object model ops

def Narval_GetFieldOp : Narval_Op<"get_field"> {
    let summary = "Read a named field from a Narval class instance";
    let description = [{
        Reads a field from an NVMap-backed class instance. Lowers to
        nv_object_get_field(object, field_name).

        Example:
            %name = narval.get_field %user["name"] : !narval.value
    }];
    let arguments = (ins
        Narval_ValueType:$object,
        StrAttr:$field_name
    );
    let results = (outs Narval_ValueType:$result);
    let assemblyFormat = "$object `[` $field_name `]` attr-dict `:` type($result)";
}

def Narval_SetFieldOp : Narval_Op<"set_field"> {
    let summary = "Write a named field on a Narval class instance";
    let description = [{
        Writes a field on an NVMap-backed class instance. Lowers to
        nv_object_set_field(object, field_name, value).
    }];
    let arguments = (ins
        Narval_ValueType:$object,
        StrAttr:$field_name,
        Narval_ValueType:$value
    );
    let assemblyFormat = "$object `[` $field_name `]` `=` $value attr-dict";
}

def Narval_CallMethodOp : Narval_Op<"call_method"> {
    let summary = "Call a method on a Narval class instance";
    let description = [{
        Dispatches a method call to __method_ClassName_name(this, args...).
        The class name is embedded as an attribute; the lowering pass emits
        a direct func.call to the mangled name.

        Example:
            narval.call_method %obj {class_name="User", method_name="to_string"}(%args) : !narval.value
    }];
    let arguments = (ins
        Narval_ValueType:$receiver,
        StrAttr:$class_name,
        StrAttr:$method_name,
        Variadic<Narval_ValueType>:$args
    );
    let results = (outs Narval_ValueType:$result);
    let hasCustomAssemblyFormat = 0;
}

// Tensor / ML ops

def Narval_TensorMatmulOp : Narval_Op<"tensor_matmul", [Pure]> {
    let summary = "Matrix multiplication: result = lhs @ rhs";
    let description = [{
        Computes the matrix product of two rank-2 tensors. Shape dimensions are
        verified at compile time when they are statically known.

        Lowers to linalg.matmul after LowerTensorToLinalgPass runs.

        Example:
            %c = narval.tensor_matmul %a, %b
                : (tensor<32x128xf32>, tensor<128x64xf32>) -> tensor<32x64xf32>
    }];
    let arguments = (ins AnyRankedTensor:$lhs, AnyRankedTensor:$rhs);
    let results = (outs AnyRankedTensor:$result);
    let assemblyFormat = [{
        $lhs `,` $rhs attr-dict `:` functional-type(operands, results)
    }];
    let hasVerifier = 1;
}

def Narval_TensorAddOp : Narval_Op<"tensor_add", [Pure]> {
    let summary = "Element-wise tensor addition";
    let description = [{
        Adds two tensors element-wise. Both operands must have the same shape
        (broadcasting is applied at compile time if shapes are known).

        Lowers to linalg.add → vector.add → LLVM vector ops (AVX/NEON).
    }];
    let arguments = (ins AnyRankedTensor:$lhs, AnyRankedTensor:$rhs);
    let results = (outs AnyRankedTensor:$result);
    let assemblyFormat = [{
        $lhs `,` $rhs attr-dict `:` functional-type(operands, results)
    }];
    let hasCanonicalizer = 1;
    let hasVerifier = 1;
}

def Narval_TensorMulOp : Narval_Op<"tensor_mul", [Pure]> {
    let summary = "Element-wise tensor multiplication";
    let arguments = (ins AnyRankedTensor:$lhs, AnyRankedTensor:$rhs);
    let results = (outs AnyRankedTensor:$result);
    let assemblyFormat = [{
        $lhs `,` $rhs attr-dict `:` functional-type(operands, results)
    }];
    let hasVerifier = 1;
}

def Narval_TensorTransposeOp : Narval_Op<"tensor_transpose", [Pure]> {
    let summary = "Transpose a tensor according to a permutation";
    let arguments = (ins
        AnyRankedTensor:$input,
        DenseI64ArrayAttr:$permutation
    );
    let results = (outs AnyRankedTensor:$result);
    let assemblyFormat = "$input `perm` `=` $permutation attr-dict `:` functional-type(operands, results)";
    let hasCanonicalizer = 1;
}

def Narval_TensorFillOp : Narval_Op<"tensor_fill"> {
    let summary = "Create a tensor filled with a scalar value";
    let arguments = (ins
        AnyType:$fill_value,
        DenseI64ArrayAttr:$shape
    );
    let results = (outs AnyRankedTensor:$result);
    let assemblyFormat = "$fill_value `shape` `=` $shape attr-dict `:` functional-type(operands, results)";
}

// GPU ops

// Marks a function as a GPU kernel.
// The body is lowered to gpu.func inside a gpu.module named $module_name.
// Target: "cuda" | "rocm" | "spirv" (default "cuda").
def Narval_GPUKernelOp : Narval_Op<"gpu_kernel"> {
    let summary = "Declare a Narval function as a GPU kernel";
    let description = [{
        Marks a function body as a GPU kernel. Lowered to gpu.func inside a
        gpu.module by LowerGPUKernelsPass. Target: "cuda" | "rocm" | "spirv".
    }];
    let arguments = (ins
        StrAttr:$sym_name,
        TypeAttrOf<FunctionType>:$function_type,
        DefaultValuedAttr<StrAttr, "\"cuda\"">:$target,
        DefaultValuedAttr<StrAttr, "\"gpu_module\"">:$module_name
    );
    let regions = (region AnyRegion:$body);
    let hasCustomAssemblyFormat = 0;
}

def Narval_GPULaunchOp : Narval_Op<"gpu_launch"> {
    let summary = "Launch a GPU kernel function";
    let description = [{
        Launches a GPU kernel with the given grid/block dimensions. Lowers to
        gpu.launch_func after LowerGPUKernelsPass.
    }];
    let arguments = (ins
        FlatSymbolRefAttr:$kernel,
        Index:$grid_x, Index:$grid_y, Index:$grid_z,
        Index:$block_x, Index:$block_y, Index:$block_z,
        Variadic<AnyType>:$kernel_operands
    );
    let hasCustomAssemblyFormat = 0;
}

def Narval_GPUThreadIdOp : Narval_Op<"gpu_thread_id", [Pure]> {
    let summary = "Get the GPU thread index along a dimension";
    let arguments = (ins StrAttr:$dimension);  // "x", "y", or "z"
    let results = (outs Index:$result);
    let assemblyFormat = "$dimension attr-dict";
}

def Narval_GPUBlockIdOp : Narval_Op<"gpu_block_id", [Pure]> {
    let summary = "Get the GPU block index along a dimension";
    let arguments = (ins StrAttr:$dimension);
    let results = (outs Index:$result);
    let assemblyFormat = "$dimension attr-dict";
}

// narval.yield — terminator for control-flow regions (if / for_range / while)
// Analogous to scf.yield. Function returns use narval.return instead.
def Narval_YieldOp : Narval_Op<"yield", [Pure, Terminator, ReturnLike]> {
    let summary = "Yield values from a control-flow region";
    let description = [{
        Terminates an if-then/else, for-range body, or while body region.
        Zero operands = void yield (loop step with no value).

        Lowers to scf.yield.
    }];
    let arguments = (ins Variadic<AnyType>:$operands);
    let assemblyFormat = "($operands^ `:` type($operands))? attr-dict";
}

// narval.if — conditional branch with optional results.
// Then and else regions must be terminated with narval.yield.
def Narval_IfOp : Narval_Op<"if", [
    RecursiveMemoryEffects, NoRegionArguments
]> {
    let summary = "Conditional branch with optional yielded values";
    let description = [{
        Evaluates `condition` (i1). Executes then_region if true, else_region
        if false. Results are yielded from whichever branch executes.

        Lowers to scf.if + scf.yield.

        Example:
            %r = narval.if %cond -> (!narval.value) {
                narval.yield %a : !narval.value
            } else {
                narval.yield %b : !narval.value
            }
    }];
    let arguments  = (ins I1:$condition);
    let results    = (outs Variadic<AnyType>:$results);
    let regions    = (region AnyRegion:$then_region, AnyRegion:$else_region);
    // Custom format mirrors scf.if
    let assemblyFormat = [{
        $condition (`->` `(` type($results)^ `)`)? $then_region
        (`else` $else_region^)? attr-dict
    }];
    let hasVerifier = 1;
}

// narval.for_range — integer-range loop (the common case for `for i in 0..N`).
// Body is terminated with narval.yield.
def Narval_ForRangeOp : Narval_Op<"for_range", [
    RecursiveMemoryEffects, SingleBlock
]> {
    let summary = "Integer range loop (lowers to scf.for)";
    let description = [{
        Iterates an integer range [lb, ub) with step `step`.
        `loop_var` is the induction variable name (for debug info only).

        Lowers to scf.for + scf.yield.

        Example:
            narval.for_range %lb to %ub step %step iter_args(%r = %init) {
            ^bb0(%i: index, %r_iter: !narval.value):
                narval.yield %new_r : !narval.value
            }
    }];
    let arguments = (ins
        Index:$lb,
        Index:$ub,
        Index:$step,
        Variadic<AnyType>:$init_args,   // initial values for loop-carried vars
        StrAttr:$loop_var               // loop variable name (for debug)
    );
    let results  = (outs Variadic<AnyType>:$results);
    let regions  = (region SizedRegion<1>:$body);
    let hasCustomAssemblyFormat = 0;   // use generic form for now
    let hasVerifier = 1;
}

// narval.while — general while loop.
// condition_region yields i1 + any loop-carried values; body_region yields
// the updated loop-carried values for the next iteration.
def Narval_WhileOp : Narval_Op<"while", [RecursiveMemoryEffects]> {
    let summary = "General while loop (lowers to scf.while)";
    let description = [{
        Executes condition_region. If it yields true (first value), runs
        body_region; otherwise exits with the remaining yielded values.

        Lowers to scf.while + scf.condition + scf.yield.
    }];
    let arguments = (ins Variadic<AnyType>:$init_args);
    let results   = (outs Variadic<AnyType>:$results);
    let regions   = (region SizedRegion<1>:$condition_region,
                            SizedRegion<1>:$body_region);
    let hasCustomAssemblyFormat = 0;
}

//===----------------------------------------------------------------------===//
// narval.func — Narval function definition with language metadata
//
// Analogous to func.func but carries Narval-specific attributes:
//   abi         — calling convention ("sysv64", "win64", "C", "")
//   is_fallible — body contains propagate → return is wrapped in Ok(...)
//   is_async    — return type is implicitly Future<T>
//   is_comptime — evaluated at compile time by ComptimeEvaluator
//   optimize    — optimization hints from @optimize { } blocks
//
// Note: CallableOpInterface must be implemented in NarvalOps.cpp.
//===----------------------------------------------------------------------===//

def Narval_FuncOp : Narval_Op<"func", [
    IsolatedFromAbove, Symbol
]> {
    let summary = "Narval function definition with language metadata";
    let description = [{
        Defines a Narval function. Carries metadata not present in func.func:
        the calling convention (abi), whether the function is fallible
        (is_fallible — its return value is automatically wrapped in Ok/Err),
        whether it is async (is_async), and whether it should be evaluated at
        compile time (is_comptime).

        Lowering: LowerNarvalToStandardPass converts narval.func to func.func,
        inserting Ok/Err wrapping for fallible functions.
    }];
    let arguments = (ins
        SymbolNameAttr:$sym_name,
        TypeAttrOf<FunctionType>:$function_type,
        DefaultValuedAttr<StrAttr, "\"\"">:$abi,
        UnitAttr:$is_fallible,
        UnitAttr:$is_async,
        UnitAttr:$is_comptime,
        OptionalAttr<Narval_OptimizeAttr>:$optimize
    );
    let regions = (region AnyRegion:$body);
    let hasCustomAssemblyFormat = 0;
}

//===----------------------------------------------------------------------===//
// narval.for_in — collection iteration (non-range)
//
// Iterates over the elements of an arbitrary !narval.value collection
// (vector, array, map). The body block receives the current element as
// its first argument (%elem: !narval.value).
// Lowers to a call to runtime iteration helpers + scf.for.
//===----------------------------------------------------------------------===//

def Narval_ForInOp : Narval_Op<"for_in", [RecursiveMemoryEffects, SingleBlock]> {
    let summary = "Iterate over a Narval collection (vector, array, map)";
    let description = [{
        for x in collection { ... }

        The body block has a single block argument (%elem: !narval.value)
        bound to the current iteration element. Loop-carried values are
        forwarded via narval.yield.

        Lowers to runtime length + index queries wrapped in scf.for.
    }];
    let arguments = (ins
        Narval_ValueType:$iterable,
        StrAttr:$loop_var           // loop variable name (debug info)
    );
    let results = (outs Variadic<AnyType>:$results);
    let regions  = (region SizedRegion<1>:$body);
    let hasCustomAssemblyFormat = 0;
}

//===----------------------------------------------------------------------===//
// narval.match — pattern matching
//
// Each arm is a separate region whose entry block begins with a pattern-check
// terminator (narval.yield with i1 condition) followed by the arm body.
// Lowers to a chain of scf.if ops.
//===----------------------------------------------------------------------===//

def Narval_MatchOp : Narval_Op<"match", [RecursiveMemoryEffects]> {
    let summary = "Pattern match over a Narval value";
    let description = [{
        match subject {
            pattern_1 => { ... }
            pattern_2 => { ... }
            _ => { ... }
        }

        Each arm is a region terminated by narval.yield carrying the arm
        result values. The entry block of each arm begins with a condition
        check; if the check fails, control falls through to the next arm.

        Lowers to a chain of scf.if + scf.yield.
    }];
    let arguments = (ins Narval_ValueType:$subject);
    let results   = (outs Variadic<AnyType>:$results);
    let regions   = (region VariadicRegion<AnyRegion>:$arms);
    let hasCustomAssemblyFormat = 0;
}

//===----------------------------------------------------------------------===//
// Error handling ops
//===----------------------------------------------------------------------===//

// narval.try_region — try/catch/finally.
// try_body     — the guarded code block
// catch_arms   — one region per catch clause (variadic)
// finally_body — always-executed cleanup region
// Lowers using setjmp/longjmp via the existing runtime mechanism.
def Narval_TryRegionOp : Narval_Op<"try_region", [RecursiveMemoryEffects]> {
    let summary = "try / catch / finally region";
    let description = [{
        Implements Narval's try/catch/finally using setjmp/longjmp:
          - try_body runs under a registered exception handler.
          - On throw, the matching catch_arm region is entered.
          - finally_body is always executed before leaving the op.

        Lowers to the same setjmp/longjmp pattern as the legacy codegen.
    }];
    let results = (outs Variadic<AnyType>:$results);
    let regions = (region
        AnyRegion:$try_body,
        VariadicRegion<AnyRegion>:$catch_arms,
        AnyRegion:$finally_body
    );
    let hasCustomAssemblyFormat = 0;
}

// narval.throw — longjmp to the nearest active handler.
def Narval_ThrowOp : Narval_Op<"throw", [Terminator]> {
    let summary = "Throw a Narval error (longjmp to active handler)";
    let description = [{
        Throws an error value, transferring control to the nearest enclosing
        narval.try_region handler. Implemented via longjmp.
        Lowers to a call to the runtime throw helper.
    }];
    let arguments = (ins Narval_ValueType:$error);
    let assemblyFormat = "$error attr-dict";
}

// narval.result_wrap — wrap a value in Ok(...) or Err(...).
// Used by fallible function lowering to box the return value.
def Narval_ResultWrapOp : Narval_Op<"result_wrap", [Pure]> {
    let summary = "Wrap a value as Ok(...) or Err(...)";
    let description = [{
        Wraps a value in the Result runtime representation.
        is_ok = true  → nv_make_ok(value)  (NV_RESULT_OK_BASE)
        is_ok = false → nv_make_err(value) (NV_RESULT_ERR_BASE)

        Inserted by LowerNarvalToStandardPass for fallible functions.
    }];
    let arguments = (ins
        Narval_ValueType:$value,
        BoolAttr:$is_ok
    );
    let results = (outs Narval_ValueType:$result);
    let hasCustomAssemblyFormat = 0;
}

// narval.propagate — re-throw if Err, unwrap if Ok.
// Implements the `propagate` keyword in fallible functions.
def Narval_PropagateOp : Narval_Op<"propagate"> {
    let summary = "Re-throw if Err, unwrap and continue if Ok";
    let description = [{
        Checks whether the input is an Err value. If so, re-throws it via
        longjmp (same mechanism as narval.throw). If Ok, yields the inner
        value as the result.

        Only valid inside fallible functions (narval.func with is_fallible).
        Lowers to: check NV_RESULT_ERR_BASE tag → conditional narval.throw.
    }];
    let arguments = (ins Narval_ValueType:$value);
    let results   = (outs Narval_ValueType:$unwrapped);
    let assemblyFormat = "$value attr-dict `:` type($unwrapped)";
}

//===----------------------------------------------------------------------===//
// Remaining tensor ops (Fase 4)
//===----------------------------------------------------------------------===//

// narval.tensor_map — apply a closure to each element.
// body region: single block, argument = one element, yield = mapped element.
// Lowers to linalg.generic with identity indexing maps.
def Narval_TensorMapOp : Narval_Op<"tensor_map", [Pure]> {
    let summary = "Apply an element-wise closure to a tensor";
    let description = [{
        tensor.map(|x| f(x))

        The mapper region has one block argument (the current element) and
        must terminate with narval.yield carrying the result element.

        Lowers to linalg.generic with identity indexing maps so that the
        closure body is inlined into each iteration of the linalg op.
    }];
    let arguments = (ins AnyRankedTensor:$input);
    let results   = (outs AnyRankedTensor:$result);
    let regions   = (region AnyRegion:$mapper);
    let hasCustomAssemblyFormat = 0;
}

// narval.tensor_reduce — reduce a tensor along one dimension.
// combiner region: two block args (accumulator, element), yield = new acc.
// Lowers to linalg.reduce.
def Narval_TensorReduceOp : Narval_Op<"tensor_reduce", [Pure]> {
    let summary = "Reduce a tensor along one axis";
    let description = [{
        tensor.reduce(init, |acc, x| f(acc, x), axis: N)

        The combiner region has two block arguments (accumulator, element)
        and must terminate with narval.yield carrying the updated accumulator.

        dimension: axis index to reduce (0-based).
        Lowers to linalg.reduce.
    }];
    let arguments = (ins
        AnyRankedTensor:$input,
        AnyType:$init,
        I64Attr:$dimension
    );
    let results = (outs AnyType:$result);
    let regions = (region AnyRegion:$combiner);
    let hasCustomAssemblyFormat = 0;
}

// narval.tensor_slice — extract a slice/view from a tensor.
// offsets, sizes, strides are Index SSA values (one per dimension).
// Lowers to tensor.extract_slice.
def Narval_TensorSliceOp : Narval_Op<"tensor_slice", [Pure]> {
    let summary = "Extract a slice or view from a tensor";
    let description = [{
        Creates a view of a sub-region of a tensor without copying data.
        offsets, sizes, and strides are Index-typed SSA values (one per
        source dimension). Lowers to tensor.extract_slice.
    }];
    let arguments = (ins
        AnyRankedTensor:$source,
        Variadic<Index>:$offsets,
        Variadic<Index>:$sizes,
        Variadic<Index>:$strides
    );
    let results = (outs AnyRankedTensor:$result);
    let hasCustomAssemblyFormat = 0;
}

//===----------------------------------------------------------------------===//
// Object model — narval.new
//===----------------------------------------------------------------------===//

// narval.new — instantiate a Narval class.
// Calls nv_alloc_object + __ctor_ClassName(this, ctor_args...).
// Result type is !narval.value (the NVMap-backed instance).
def Narval_NewOp : Narval_Op<"new"> {
    let summary = "Instantiate a Narval class";
    let description = [{
        Allocates and initializes a new class instance by calling:
          1. nv_alloc_object() to create the NVMap-backed !narval.value.
          2. __ctor_ClassName(this, ctor_args...) to run the constructor.

        Lowers in LowerNarvalToStandardPass to the two runtime calls above.
    }];
    let arguments = (ins
        StrAttr:$class_name,
        Variadic<Narval_ValueType>:$ctor_args
    );
    let results = (outs Narval_ValueType:$instance);
    let hasCustomAssemblyFormat = 0;
}

//===----------------------------------------------------------------------===//
// Comptime — narval.comptime_call
//===----------------------------------------------------------------------===//

// narval.comptime_call — compile-time function invocation.
// Always folded to narval.comptime_const during canonicalization
// (the result is already known at IR-build time).
def Narval_ComptimeCallOp : Narval_Op<"comptime_call", [Pure]> {
    let summary = "Compile-time function call (folded to comptime_const)";
    let description = [{
        Represents a call to a comptime function whose result is already
        evaluated by the ComptimeEvaluator. The canonicalizer folds this op
        into narval.comptime_const so it disappears before LLVM lowering.
    }];
    let arguments = (ins
        FlatSymbolRefAttr:$callee,
        Variadic<AnyType>:$args
    );
    let results = (outs Variadic<AnyType>:$results);
    let hasCanonicalizer = 1;
    let assemblyFormat = [{
        $callee `(` $args `)` attr-dict `:` functional-type($args, results)
    }];
}

#endif // NARVAL_OPS_TD
