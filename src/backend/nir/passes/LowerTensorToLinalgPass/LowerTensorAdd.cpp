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
// narval.tensor_add → tensor.empty + linalg.add
//===----------------------------------------------------------------------===//

struct LowerTensorAdd : public OpRewritePattern<TensorAddOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorAddOp op,
                                  PatternRewriter& r) const override {
        auto res_type = mlir::dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto loc    = op.getLoc();
        auto empty  = make_empty_tensor(r, loc, res_type);
        SmallVector<Type>  types   = {res_type};
        SmallVector<Value> inputs  = {op.getLhs(), op.getRhs()};
        SmallVector<Value> outputs = {empty};
        auto add = linalg::AddOp::create(r, loc, types, inputs, outputs);
        r.replaceOp(op, add.getResult(0));
        return success();
    }
};

} // namespace

void populateLowerTensorAdd(RewritePatternSet& patterns, MLIRContext* ctx) {
    patterns.add<LowerTensorAdd>(ctx);
}

} // namespace nv
