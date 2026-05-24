// LowerGPUKernelsPass.cpp — Fase 8: GPU kernel lowering.
//
// narval.gpu_kernel → gpu.func inside gpu.module
// narval.gpu_launch → gpu.launch_func
// narval.gpu_thread_id/block_id → gpu.thread_id/block_id
//
// Threading::DISABLED safe: uses module.getOps<> (not walk) for kernel
// discovery; only dialect conversion (applyPartialConversion) for the rest.

#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
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
        auto dim = op.getDimension() == "x" ? gpu::Dimension::x :
                   op.getDimension() == "y" ? gpu::Dimension::y :
                                              gpu::Dimension::z;
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
        auto dim = op.getDimension() == "x" ? gpu::Dimension::x :
                   op.getDimension() == "y" ? gpu::Dimension::y :
                                              gpu::Dimension::z;
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
        auto sym = SymbolRefAttr::get(r.getContext(), "gpu_module",
            {FlatSymbolRefAttr::get(r.getContext(), op.getKernel())});
        gpu::LaunchFuncOp::create(r, op.getLoc(), sym,
            gpu::KernelDim3{adaptor.getGridX(),  adaptor.getGridY(),  adaptor.getGridZ()},
            gpu::KernelDim3{adaptor.getBlockX(), adaptor.getBlockY(), adaptor.getBlockZ()},
            /*dynamicSharedMemorySize=*/nullptr,
            adaptor.getKernelOperands());
        r.eraseOp(op);
        return success();
    }
};

//===----------------------------------------------------------------------===//
// Helper: lower narval.gpu_kernel → gpu.func inside a gpu.module
// Called directly from the pass (no pattern machinery — avoids walk).
//===----------------------------------------------------------------------===//

static void lower_gpu_kernel(GPUKernelOp kernel, ModuleOp parent) {
    OpBuilder b(parent.getContext());
    auto loc = kernel.getLoc();
    llvm::StringRef mod_name = kernel.getModuleName();

    // Find or create the gpu.module sibling of the kernel op.
    gpu::GPUModuleOp gpu_mod = nullptr;
    for (auto child : parent.getOps<gpu::GPUModuleOp>()) {
        if (child.getName() == mod_name) { gpu_mod = child; break; }
    }
    if (!gpu_mod) {
        b.setInsertionPointToEnd(parent.getBody());
        gpu_mod = gpu::GPUModuleOp::create(b, loc, mod_name);
    }

    // Create gpu.func inside the gpu.module.
    b.setInsertionPointToEnd(gpu_mod.getBody());
    auto gpu_fn = gpu::GPUFuncOp::create(b, loc,
                      kernel.getSymName(), kernel.getFunctionType());
    gpu_fn->setAttr(gpu::GPUDialect::getKernelFuncAttrName(),
                    b.getUnitAttr());

    // Move the body region.
    IRMapping mapping;
    kernel.getBody().cloneInto(&gpu_fn.getBody(), mapping);

    kernel.erase();
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct LowerNarvalGPUPassImpl
    : public ::impl::LowerNarvalGPUPassBase<LowerNarvalGPUPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        MLIRContext* ctx = &getContext();

        // Phase A: lower narval.gpu_kernel ops.
        // Collect first (erasing modifies the op list).
        // Use getOps<> on the module body (not walk — safe with Threading::DISABLED).
        SmallVector<GPUKernelOp> kernels;
        for (auto k : module.getOps<GPUKernelOp>())
            kernels.push_back(k);
        for (GPUKernelOp k : kernels)
            lower_gpu_kernel(k, module);

        // Phase B: dialect conversion for launch/thread_id/block_id.
        TypeConverter tc;
        tc.addConversion([](Type t) { return t; });

        ConversionTarget target(*ctx);
        target.addLegalDialect<gpu::GPUDialect, func::FuncDialect>();
        target.addLegalOp<ModuleOp>();
        target.addIllegalOp<GPULaunchOp, GPUThreadIdOp, GPUBlockIdOp>();
        target.markUnknownOpDynamicallyLegal([](Operation*) { return true; });

        RewritePatternSet patterns(ctx);
        patterns.add<LowerGPULaunch, LowerGPUThreadId, LowerGPUBlockId>(tc, ctx);

        if (failed(applyPartialConversion(module, target, std::move(patterns))))
            signalPassFailure();
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerNarvalGPUPass() {
    return std::make_unique<LowerNarvalGPUPassImpl>();
}

} // namespace nv
