#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/Vector/IR/VectorOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

#define GEN_PASS_DEF_NARVALLINALGVECTORIZEPASS
#include "NarvalPasses.h.inc"

using namespace mlir;

namespace nv {
namespace {

struct NarvalLinalgVectorizePassImpl
    : public ::impl::NarvalLinalgVectorizePassBase<NarvalLinalgVectorizePassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        IRRewriter rewriter(&getContext());

        // Iterate via getOps (safe with Threading::DISABLED — no walk()).
        for (auto func : module.getOps<func::FuncOp>()) {
            if (func.isExternal() || func.getBody().empty()) continue;

            // Collect linalg ops from the entry block only (kernels are flat).
            SmallVector<linalg::LinalgOp> to_vectorize;
            Block& entry = func.getBody().front();
            for (Operation& op : entry.getOperations()) {
                if (auto lop = dyn_cast<linalg::LinalgOp>(&op))
                    to_vectorize.push_back(lop);
            }

            // Vectorize each op.  linalg::vectorize uses the rewriter directly
            // (no applyPatternsGreedily) so it is safe with Threading::DISABLED.
            for (linalg::LinalgOp lop : to_vectorize) {
                rewriter.setInsertionPoint(lop);
                // vectorize() returns FailureOr<VectorizationResult>.
                // Pass empty inputVectorSizes → MLIR infers from static shapes.
                auto result = linalg::vectorize(rewriter, lop.getOperation(),
                                                /*inputVectorSizes=*/{},
                                                /*inputScalableVecDims=*/{});
                (void)result;  // fallback to loops on failure (see pipeline)
            }
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createNarvalLinalgVectorizePass() {
    return std::make_unique<NarvalLinalgVectorizePassImpl>();
}

} // namespace nv
