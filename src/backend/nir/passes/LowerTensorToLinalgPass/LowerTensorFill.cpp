#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/passes/TensorLoweringHelpers.hpp"

#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/PatternMatch.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// narval.tensor_fill → tensor.empty + linalg.fill
//===----------------------------------------------------------------------===//

struct LowerTensorFill : public OpRewritePattern<TensorFillOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorFillOp op,
                                  PatternRewriter& r) const override {
        auto res_type = mlir::dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto loc    = op.getLoc();
        auto empty  = make_empty_tensor(r, loc, res_type);
        SmallVector<Value> inputs = {op.getFillValue()};
        SmallVector<Value> outputs = {empty};
        auto filled = linalg::FillOp::create(r, loc, inputs, outputs);
        r.replaceOp(op, filled.getResult(0));
        return success();
    }
};

} // namespace

void populateLowerTensorFill(RewritePatternSet& patterns, MLIRContext* ctx) {
    patterns.add<LowerTensorFill>(ctx);
}

} // namespace nv
