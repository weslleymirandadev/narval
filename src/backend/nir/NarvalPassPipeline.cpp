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
    pm.addPass(mlir::createCSEPass());
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
}

void build_narval_pass_pipeline_phase_b(mlir::PassManager& pm) {
    pm.enableVerifier(false);
    pm.addPass(nv::createNarvalLinalgVectorizePass());
    pm.addPass(mlir::bufferization::createOneShotBufferizePass());
    pm.addPass(mlir::bufferization::createOwnershipBasedBufferDeallocationPass());
    pm.addPass(mlir::bufferization::createBufferDeallocationSimplificationPass());
    pm.addPass(mlir::createConvertLinalgToLoopsPass());
    pm.addPass(mlir::createSCFToControlFlowPass());
    pm.addPass(mlir::createConvertVectorToLLVMPass());
    pm.addPass(nv::createLowerNarvalToLLVMPass());
    pm.addPass(mlir::createReconcileUnrealizedCastsPass());
}

} // namespace nv
