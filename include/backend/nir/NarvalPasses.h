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
std::unique_ptr<mlir::Pass> createLowerNarvalControlFlowPass();
std::unique_ptr<mlir::Pass> createLowerNarvalToStandardPass();
std::unique_ptr<mlir::Pass> createLowerNarvalTensorPass();
std::unique_ptr<mlir::Pass> createLowerNarvalGPUPass();
std::unique_ptr<mlir::Pass> createLowerNarvalToLLVMPass();

void apply_transform_annotations(mlir::ModuleOp module, mlir::PassManager& pm);

} // namespace nv

#define GEN_PASS_DECL
#include "NarvalPasses.h.inc"
