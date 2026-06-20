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
// narval.tensor_mul → tensor.empty + linalg.mul
//===----------------------------------------------------------------------===//

struct LowerTensorMul : public OpRewritePattern<TensorMulOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorMulOp op,
                                  PatternRewriter& r) const override {
        auto res_type = mlir::dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto loc    = op.getLoc();
        auto empty  = make_empty_tensor(r, loc, res_type);
        SmallVector<Type>  types   = {res_type};
        SmallVector<Value> inputs  = {op.getLhs(), op.getRhs()};
        SmallVector<Value> outputs = {empty};
        auto mul = linalg::MulOp::create(r, loc, types, inputs, outputs);
        r.replaceOp(op, mul.getResult(0));
        return success();
    }
};

} // namespace

void populateLowerTensorMul(RewritePatternSet& patterns, MLIRContext* ctx) {
    patterns.add<LowerTensorMul>(ctx);
}

} // namespace nv
