#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/Passes.h"

#define GEN_PASS_DEF_LOWERNARVALTOSTANDARDPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {

// Forward declarations for populate* functions (defined in individual pattern .cpp files).
void populateLowerAllocOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerDropOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerMoveOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerBorrowOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerBorrowMutOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerNarvalCallOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerCallOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerCallRuntimeOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerReturnOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerConstantOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerComptimeConstOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerNewOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerGetFieldOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerSetFieldOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);
void populateLowerCallMethodOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc);

namespace {

//===----------------------------------------------------------------------===//
// The pass struct
//===----------------------------------------------------------------------===//

struct LowerNarvalToStandardPassImpl
    : public ::impl::LowerNarvalToStandardPassBase<LowerNarvalToStandardPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        MLIRContext* ctx = &getContext();
        NarvalTypeConverter tc(ctx);
        ConversionTarget target(*ctx);

        target.addLegalDialect<func::FuncDialect, arith::ArithDialect,
                               scf::SCFDialect, memref::MemRefDialect,
                               LLVM::LLVMDialect>();
        target.addLegalOp<ModuleOp>();
        // Mark func.call as dynamically legal — illegal when it has narval types.
        target.addDynamicallyLegalOp<func::CallOp>([](func::CallOp op) {
            for (auto t : op.getOperandTypes())
                if (mlir::isa<narval::ValueType, narval::RefType, narval::MutRefType>(t))
                    return false;
            for (auto t : op.getResultTypes())
                if (mlir::isa<narval::ValueType, narval::RefType, narval::MutRefType>(t))
                    return false;
            return true;
        });
        target.addIllegalOp<AllocOp, DropOp, MoveOp, BorrowOp, BorrowMutOp,
                            CallOp, CallRuntimeOp, ReturnOp,
                            ConstantOp, ComptimeConstOp,
                            NewOp, GetFieldOp, SetFieldOp, CallMethodOp>();
        // Tensor and GPU stay for later passes.
        target.addLegalOp<TensorMatmulOp, TensorAddOp, TensorMulOp,
                          TensorTransposeOp, TensorFillOp,
                          GPULaunchOp, GPUThreadIdOp, GPUBlockIdOp>();
        // CF ops are already lowered by LowerNarvalControlFlowPass.
        target.addLegalOp<IfOp, ForRangeOp, WhileOp, YieldOp>();

        RewritePatternSet patterns(ctx);
        populateLowerAllocOp(patterns, tc);
        populateLowerDropOp(patterns, tc);
        populateLowerMoveOp(patterns, tc);
        populateLowerBorrowOp(patterns, tc);
        populateLowerBorrowMutOp(patterns, tc);
        populateLowerNarvalCallOp(patterns, tc);
        populateLowerCallOp(patterns, tc);
        populateLowerCallRuntimeOp(patterns, tc);
        populateLowerReturnOp(patterns, tc);
        populateLowerConstantOp(patterns, tc);
        populateLowerComptimeConstOp(patterns, tc);
        populateLowerNewOp(patterns, tc);
        populateLowerGetFieldOp(patterns, tc);
        populateLowerSetFieldOp(patterns, tc);
        populateLowerCallMethodOp(patterns, tc);

        if (failed(applyPartialConversion(module, target, std::move(patterns)))) {
            module.emitError("lower-narval-to-std: conversion failed");
            signalPassFailure();
        }
    }
};

} // namespace

//===----------------------------------------------------------------------===//
// Public creation functions for all passes in this file
//===----------------------------------------------------------------------===//

std::unique_ptr<mlir::Pass> createLowerNarvalToStandardPass() {
    return std::make_unique<LowerNarvalToStandardPassImpl>();
}

} // namespace nv
