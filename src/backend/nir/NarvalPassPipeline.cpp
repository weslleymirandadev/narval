// NarvalPassPipeline.cpp — Build the Narval NIR lowering pass pipelines.
//
// Phase A: narval dialect → func/arith/scf/llvm (narval-specific lowering)
// Phase B: func/arith/scf/linalg/vector → llvm dialect (standard lowerings)
//
// Extracted from NIRGenerationContext::lower_to_llvm_ir so that the pipeline
// can be shared by the CLI, JIT, and any future test harness.

#include "backend/nir/NarvalPasses.h"

#include "mlir/Conversion/LinalgToStandard/LinalgToStandard.h"
#include "mlir/Conversion/Passes.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Conversion/VectorToLLVM/ConvertVectorToLLVMPass.h"
#include "mlir/Dialect/Bufferization/Transforms/Passes.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"


namespace nv {

void build_narval_pass_pipeline_phase_a(mlir::PassManager& pm,
                                        mlir::ModuleOp module) {
    pm.enableVerifier(false);
    pm.addPass(nv::createLowerNarvalFunctionsPass());
    pm.addPass(nv::createNarvalCanonicalizationPass());
    // NOTE: mlir::createCSEPass is NOT run: its dead-op elimination treats ops
    // without a MemoryEffectOpInterface as effect-free, so control-flow ops
    // (narval.if/while — RecursiveMemoryEffects only) with no results were
    // erased along with their regions (bodies containing func.return vanished,
    // leaving only the condition computation). Re-enable only with a filter
    // that preserves region-bearing ops.
    pm.addPass(mlir::createSymbolDCEPass());
    nv::apply_transform_annotations(module, pm);
    pm.addPass(nv::createNarvalOwnershipPass());
    pm.addPass(nv::createLowerNarvalTensorPass());
    pm.addPass(nv::createLowerNarvalControlFlowPass());
    pm.addPass(nv::createLowerNarvalErrorHandlingPass());
    pm.addPass(nv::createLowerNarvalToStandardPass());
    pm.addPass(nv::createFixSCFIfTypesPass());
    pm.addPass(nv::createLowerNarvalGPUPass());
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());
    pm.addPass(nv::createInsertRuntimeDropsPass());
}

static bool module_has_tensors(mlir::ModuleOp module) {
    bool found = false;
    module.walk([&](mlir::Operation* op) {
        for (mlir::Type t : op->getResultTypes()) {
            if (mlir::isa<mlir::RankedTensorType, mlir::UnrankedTensorType>(t)) {
                found = true;
                return;
            }
        }
        for (mlir::Type t : op->getOperandTypes()) {
            if (mlir::isa<mlir::RankedTensorType, mlir::UnrankedTensorType>(t)) {
                found = true;
                return;
            }
        }
    });
    return found;
}

void build_narval_pass_pipeline_phase_b(mlir::PassManager& pm,
                                        mlir::ModuleOp module) {
    pm.enableVerifier(false);
    pm.addPass(nv::createNarvalLinalgVectorizePass());
    // Bufferization is only meaningful when the module actually has tensor
    // IR. Running OneShotBufferize unconditionally rejects cf.cond_br CFGs
    // ("BranchOpInterface operations with multiple successors are not
    // supported yet") — those appear as soon as a function body uses an
    // early-return if (lowered straight to cf in phase A).
    if (module_has_tensors(module)) {
        pm.addPass(mlir::bufferization::createOneShotBufferizePass());
        pm.addPass(mlir::bufferization::createOwnershipBasedBufferDeallocationPass());
        pm.addPass(mlir::bufferization::createBufferDeallocationSimplificationPass());
        // Lower bufferization.dealloc to memref.dealloc + helpers; the final
        // LowerNarvalToLLVMPass full conversion cannot legalize the former.
        pm.addPass(mlir::bufferization::createLowerDeallocationsPass());
    }
    pm.addPass(mlir::createConvertLinalgToLoopsPass());
    pm.addPass(mlir::createSCFToControlFlowPass());
    pm.addPass(mlir::createConvertVectorToLLVMPass());
    pm.addPass(nv::createLowerNarvalToLLVMPass());
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());
}
} // namespace nv
