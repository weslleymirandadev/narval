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
// narval.tensor_slice → tensor.extract_slice
//===----------------------------------------------------------------------===//

struct LowerTensorSlice : public OpRewritePattern<TensorSliceOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorSliceOp op,
                                  PatternRewriter& r) const override {
        auto res_type = dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();
        auto slice = tensor::ExtractSliceOp::create(
            r, op.getLoc(), res_type, op.getSource(), op.getOffsets(),
            op.getSizes(), op.getStrides(), ArrayRef<NamedAttribute>{});
        r.replaceOp(op, slice.getResult());
        return success();
    }
};

} // namespace

void populateLowerTensorSlice(RewritePatternSet& patterns, MLIRContext* ctx) {
    patterns.add<LowerTensorSlice>(ctx);
}

} // namespace nv
