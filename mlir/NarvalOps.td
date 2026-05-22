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
    // Use generic assembly form — custom printer/parser deferred to Phase 2.
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
    // Generic form for Phase 1; custom printer deferred.
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

//===----------------------------------------------------------------------===//
// Phase 2 — Control Flow ops
//===----------------------------------------------------------------------===//

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

#endif // NARVAL_OPS_TD
