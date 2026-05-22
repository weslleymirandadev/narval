#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#define GEN_PASS_DEF_LOWERNARVALGPUPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// narval.gpu_thread_id → gpu.thread_id
//===----------------------------------------------------------------------===//

struct LowerGPUThreadId : public OpConversionPattern<GPUThreadIdOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(GPUThreadIdOp op, OpAdaptor,
                                  ConversionPatternRewriter& r) const override {
        gpu::Dimension dim =
            op.getDimension() == "x" ? gpu::Dimension::x :
            op.getDimension() == "y" ? gpu::Dimension::y : gpu::Dimension::z;
        r.replaceOpWithNewOp<gpu::ThreadIdOp>(op, dim);
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.gpu_block_id → gpu.block_id
//===----------------------------------------------------------------------===//

struct LowerGPUBlockId : public OpConversionPattern<GPUBlockIdOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(GPUBlockIdOp op, OpAdaptor,
                                  ConversionPatternRewriter& r) const override {
        gpu::Dimension dim =
            op.getDimension() == "x" ? gpu::Dimension::x :
            op.getDimension() == "y" ? gpu::Dimension::y : gpu::Dimension::z;
        r.replaceOpWithNewOp<gpu::BlockIdOp>(op, dim);
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.gpu_launch → gpu.launch_func
//===----------------------------------------------------------------------===//

struct LowerGPULaunch : public OpConversionPattern<GPULaunchOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(GPULaunchOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        // Build the gpu.launch_func call.
        // The kernel symbol must be in a gpu.module (placed by LowerGPUKernel).
        auto sym = SymbolRefAttr::get(r.getContext(), "gpu_module",
            {FlatSymbolRefAttr::get(r.getContext(), op.getKernel())});

        r.create<gpu::LaunchFuncOp>(op.getLoc(),
            sym,
            gpu::KernelDim3{adaptor.getGridX(),
                             adaptor.getGridY(),
                             adaptor.getGridZ()},
            gpu::KernelDim3{adaptor.getBlockX(),
                             adaptor.getBlockY(),
                             adaptor.getBlockZ()},
            /*dynamicSharedMemorySize=*/nullptr,
            adaptor.getKernelOperands());
        r.eraseOp(op);
        return success();
    }
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct LowerNarvalGPUPassImpl
    : public ::impl::LowerNarvalGPUPassBase<LowerNarvalGPUPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        MLIRContext* ctx = &getContext();

        TypeConverter tc;
        tc.addConversion([](Type t) { return t; });

        ConversionTarget target(*ctx);
        target.addLegalDialect<gpu::GPUDialect, func::FuncDialect>();
        target.addLegalOp<ModuleOp>();
        target.addIllegalOp<GPULaunchOp, GPUThreadIdOp, GPUBlockIdOp>();
        target.markUnknownOpDynamicallyLegal([](Operation*) { return true; });

        RewritePatternSet patterns(ctx);
        patterns.add<LowerGPULaunch, LowerGPUThreadId, LowerGPUBlockId>(tc, ctx);

        if (failed(applyPartialConversion(module, target, std::move(patterns)))) {
            module.emitError("lower-narval-gpu: conversion failed");
            signalPassFailure();
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerNarvalGPUPass() {
    return std::make_unique<LowerNarvalGPUPassImpl>();
}

} // namespace nv
