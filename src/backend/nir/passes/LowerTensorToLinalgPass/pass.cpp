#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#define GEN_PASS_DEF_LOWERNARVALTENSORPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {

// Forward declarations for populate* functions (defined in individual pattern .cpp files).
void populateLowerTensorFill(RewritePatternSet& patterns, MLIRContext* ctx);
void populateLowerTensorMatmul(RewritePatternSet& patterns, MLIRContext* ctx);
void populateLowerTensorAdd(RewritePatternSet& patterns, MLIRContext* ctx);
void populateLowerTensorMul(RewritePatternSet& patterns, MLIRContext* ctx);
void populateLowerTensorTranspose(RewritePatternSet& patterns, MLIRContext* ctx);
void populateLowerTensorMap(RewritePatternSet& patterns, MLIRContext* ctx);
void populateLowerTensorReduce(RewritePatternSet& patterns, MLIRContext* ctx);
void populateLowerTensorSlice(RewritePatternSet& patterns, MLIRContext* ctx);

namespace {

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct LowerNarvalTensorPassImpl
    : public ::impl::LowerNarvalTensorPassBase<LowerNarvalTensorPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        // Iterate top-level func ops without walk() to avoid TypeID-lookup issues.
        for (Operation& op : module.getBody()->getOperations()) {
            if (op.getName().getStringRef() != "func.func") continue;
            if (op.getNumRegions() == 0 || op.getRegion(0).empty()) continue;
            MLIRContext* ctx = &getContext();
            RewritePatternSet patterns(ctx);
            populateLowerTensorFill(patterns, ctx);
            populateLowerTensorMatmul(patterns, ctx);
            populateLowerTensorAdd(patterns, ctx);
            populateLowerTensorMul(patterns, ctx);
            populateLowerTensorTranspose(patterns, ctx);
            populateLowerTensorMap(patterns, ctx);
            populateLowerTensorReduce(patterns, ctx);
            populateLowerTensorSlice(patterns, ctx);
            // Only run if there are tensor ops in this func.
            bool has_tensor = false;
            for (Block& block : op.getRegion(0)) {
                for (Operation& nested : block.getOperations()) {
                    if (isa<TensorMatmulOp, TensorAddOp, TensorMulOp,
                            TensorTransposeOp, TensorFillOp, TensorMapOp,
                            TensorReduceOp, TensorSliceOp>(nested)) {
                        has_tensor = true;
                        break;
                    }
                }
                if (has_tensor) break;
            }
            if (!has_tensor) continue;
            if (failed(applyPatternsGreedily(&op, std::move(patterns)))) {
                op.emitError("lower-narval-tensor: pattern application failed");
                signalPassFailure();
                return;
            }
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerNarvalTensorPass() {
    return std::make_unique<LowerNarvalTensorPassImpl>();
}

} // namespace nv
