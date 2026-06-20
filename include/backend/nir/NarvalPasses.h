#pragma once

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include <memory>

namespace nv {

//  Pass creation functions
std::unique_ptr<mlir::Pass> createNarvalCanonicalizationPass();
std::unique_ptr<mlir::Pass> createNarvalOwnershipPass();
std::unique_ptr<mlir::Pass> createLowerNarvalFunctionsPass();
std::unique_ptr<mlir::Pass> createLowerNarvalControlFlowPass();
std::unique_ptr<mlir::Pass> createLowerNarvalClassesPass();
std::unique_ptr<mlir::Pass> createLowerNarvalErrorHandlingPass();
std::unique_ptr<mlir::Pass> createLowerNarvalToStandardPass();
std::unique_ptr<mlir::Pass> createLowerNarvalTensorPass();
std::unique_ptr<mlir::Pass> createLowerNarvalGPUPass();
std::unique_ptr<mlir::Pass> createLowerNarvalToLLVMPass();
std::unique_ptr<mlir::Pass> createNarvalLinalgVectorizePass();   // Fase 5: linalg→vector
std::unique_ptr<mlir::Pass> createNarvalVectorLoweringPass();    // Fase 5: vector chain lowering
std::unique_ptr<mlir::Pass> createFixSCFIfTypesPass();           // Fix scf.if result types

void apply_transform_annotations(mlir::ModuleOp module, mlir::PassManager& pm);

//  Pipeline builders (NarvalPassPipeline.cpp)
void build_narval_pass_pipeline_phase_a(mlir::PassManager& pm, mlir::ModuleOp module);
void build_narval_pass_pipeline_phase_b(mlir::PassManager& pm);

} // namespace nv

#define GEN_PASS_DECL
#include "NarvalPasses.h.inc"
