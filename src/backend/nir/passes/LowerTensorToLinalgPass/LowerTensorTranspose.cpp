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
// narval.tensor_transpose → tensor.empty + linalg.transpose
//===----------------------------------------------------------------------===//

struct LowerTensorTranspose : public OpRewritePattern<TensorTransposeOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorTransposeOp op,
                                  PatternRewriter& r) const override {
        auto src_type = mlir::dyn_cast<RankedTensorType>(op.getInput().getType());
        if (!src_type) return failure();
        auto loc  = op.getLoc();
        auto perm = op.getPermutation();

        // Compute result type by permuting dims.
        SmallVector<int64_t> res_shape(src_type.getRank());
        for (int i = 0; i < (int)perm.size(); ++i)
            res_shape[i] = src_type.getDimSize(perm[i]);
        auto empty = tensor::EmptyOp::create(r, loc, res_shape,
                                              src_type.getElementType());
        SmallVector<int64_t> perm_vec(perm.begin(), perm.end());
        linalg::TransposeOp::create(
            r, loc, op.getInput(), empty.getResult(), perm_vec);
        // linalg.transpose updates the output tensor in-place; the result is the init.
        r.replaceOp(op, empty.getResult());
        return success();
    }
};

} // namespace

void populateLowerTensorTranspose(RewritePatternSet& patterns, MLIRContext* ctx) {
    patterns.add<LowerTensorTranspose>(ctx);
}

} // namespace nv
