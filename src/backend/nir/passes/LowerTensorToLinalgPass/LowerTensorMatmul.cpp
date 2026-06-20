#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/passes/TensorLoweringHelpers.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/PatternMatch.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// narval.tensor_matmul → tensor.empty + linalg.fill(0) + linalg.matmul
//===----------------------------------------------------------------------===//

struct LowerTensorMatmul : public OpRewritePattern<TensorMatmulOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorMatmulOp op,
                                  PatternRewriter& r) const override {
        auto res_type = mlir::dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto loc = op.getLoc();

        // Zero-initialise the output tensor.
        auto empty = make_empty_tensor(r, loc, res_type);
        auto zero_attr = r.getZeroAttr(res_type.getElementType());
        auto zero = arith::ConstantOp::create(r, loc, zero_attr);
        SmallVector<Value> fill_in  = {zero.getResult()};
        SmallVector<Value> fill_out = {empty};
        auto init = linalg::FillOp::create(r, loc, fill_in, fill_out).getResult(0);

        SmallVector<Type>  types   = {res_type};
        SmallVector<Value> inputs  = {op.getLhs(), op.getRhs()};
        SmallVector<Value> outputs = {init};
        auto matmul = linalg::MatmulOp::create(r, loc, types, inputs, outputs);
        r.replaceOp(op, matmul.getResult(0));
        return success();
    }
};

} // namespace

void populateLowerTensorMatmul(RewritePatternSet& patterns, MLIRContext* ctx) {
    patterns.add<LowerTensorMatmul>(ctx);
}

} // namespace nv
