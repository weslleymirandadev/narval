#ifndef NARVAL_PASSES_TD
#define NARVAL_PASSES_TD

include "mlir/Pass/PassBase.td"

// Canonicalization

def NarvalCanonicalizationPass : Pass<"narval-canonicalize", "::mlir::ModuleOp"> {
    let summary = "Fold and simplify narval.* operations";
    let description = [{
        Applies dialect-specific simplification patterns:
          - narval.tensor_add(x, 0) → x
          - narval.tensor_mul(x, 1) → x
          - narval.tensor_transpose(narval.tensor_transpose(x, p), inv(p)) → x
          - narval.move(narval.borrow(x)) → x
          - narval.comptime_const → fold to literal constant
        Runs to a fixpoint or until the rewrite limit is reached.
    }];
    let constructor = "nv::createNarvalCanonicalizationPass()";
}


def NarvalOwnershipPass : Pass<"narval-ownership", "::mlir::ModuleOp"> {
    let summary = "Insert narval.drop at last-use points of owned values";
    let description = [{
        Analyses def-use chains of narval.alloc results and inserts narval.drop
        at the dominance frontier of the last use of each value.
        Conservative version: inserts drops at end of defining block when the
        value does not escape (is not passed to calls or returned).
        The subsequent LowerNarvalToStandardPass converts drops to nv_free calls.
    }];
    let constructor = "nv::createNarvalOwnershipPass()";
}

def LowerNarvalControlFlowPass : Pass<"lower-narval-cf", "::mlir::ModuleOp"> {
    let summary = "Lower narval control-flow ops to scf/affine dialects";
    let description = [{
        Converts Narval control-flow operations to the SCF dialect:
          - narval.if        → scf.if     + scf.yield
          - narval.for_range → scf.for    + scf.yield
          - narval.while     → scf.while  + scf.condition + scf.yield
          - narval.yield     → scf.yield  (inside control-flow regions)
        narval.return is left for LowerNarvalToStandardPass to handle.
    }];
    let dependentDialects = [
        "::mlir::scf::SCFDialect"
    ];
    let constructor = "nv::createLowerNarvalControlFlowPass()";
}

def LowerNarvalToStandardPass : Pass<"lower-narval-to-std", "::mlir::ModuleOp"> {
    let summary = "Lower narval.* ops to standard MLIR dialects";
    let description = [{
        Converts Narval-specific operations to combinations of standard MLIR
        dialects (func, arith, scf, memref):
          - narval.call        → func.call
          - narval.return      → func.return
          - narval.call_runtime → func.call (direct runtime symbol)
          - narval.get_field   → func.call @nv_object_get_field
          - narval.set_field   → func.call @nv_object_set_field
          - narval.call_method → func.call @__method_ClassName_name
          - narval.alloc       → func.call @nv_alloc_*
          - narval.drop        → func.call @nv_free
          - narval.move        → identity (ownership analysis already done)
          - narval.constant    → arith.constant (for numeric types)
          - narval.comptime_const → arith.constant (already evaluated)
    }];
    let dependentDialects = [
        "::mlir::func::FuncDialect",
        "::mlir::arith::ArithDialect",
        "::mlir::scf::SCFDialect",
        "::mlir::memref::MemRefDialect",
        "::mlir::LLVM::LLVMDialect"
    ];
    let constructor = "nv::createLowerNarvalToStandardPass()";
}

//===----------------------------------------------------------------------===//
// Lowering: Tensor → Linalg
//===----------------------------------------------------------------------===//

def LowerNarvalTensorPass : Pass<"lower-narval-tensor", "::mlir::ModuleOp"> {
    let summary = "Lower narval.tensor_* ops to linalg dialect";
    let description = [{
        Converts high-level Narval tensor operations to linalg dialect ops
        suitable for tiling, fusion, and vectorization:
          - narval.tensor_matmul → linalg.matmul
          - narval.tensor_add    → linalg.add
          - narval.tensor_mul    → linalg.mul
          - narval.tensor_fill   → tensor.empty + linalg.fill
          - narval.tensor_transpose → linalg.transpose
    }];
    let dependentDialects = [
        "::mlir::linalg::LinalgDialect",
        "::mlir::tensor::TensorDialect",
        "::mlir::arith::ArithDialect"
    ];
    let constructor = "nv::createLowerNarvalTensorPass()";
}

//===----------------------------------------------------------------------===//
// Lowering: GPU
//===----------------------------------------------------------------------===//

def LowerNarvalGPUPass : Pass<"lower-narval-gpu", "::mlir::ModuleOp"> {
    let summary = "Lower narval.gpu_* ops to gpu dialect";
    let description = [{
        Converts Narval GPU operations to the MLIR gpu dialect:
          - narval.gpu_launch    → gpu.launch_func
          - narval.gpu_thread_id → gpu.thread_id
          - narval.gpu_block_id  → gpu.block_id
    }];
    let dependentDialects = [
        "::mlir::gpu::GPUDialect"
    ];
    let constructor = "nv::createLowerNarvalGPUPass()";
    let options = [
        Option<"target", "target", "std::string", "\"cuda\"",
               "GPU backend target: cuda, rocm, spirv">
    ];
}

//===----------------------------------------------------------------------===//
// Lowering: All → LLVM
//===----------------------------------------------------------------------===//

def LowerNarvalToLLVMPass : Pass<"lower-narval-to-llvm", "::mlir::ModuleOp"> {
    let summary = "Lower all remaining standard ops to LLVM dialect";
    let description = [{
        Final lowering step: converts func/arith/scf/memref/vector ops to the
        LLVM dialect, after which mlir::translateModuleToLLVMIR produces
        LLVM IR compatible with the existing Narval runtime linkage.
    }];
    let dependentDialects = [
        "::mlir::LLVM::LLVMDialect"
    ];
    let constructor = "nv::createLowerNarvalToLLVMPass()";
}

#endif // NARVAL_PASSES_TD
