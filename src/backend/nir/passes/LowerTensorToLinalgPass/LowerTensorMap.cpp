#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/passes/TensorLoweringHelpers.hpp"

#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Tensor/IR/Tensor.h"
#include "mlir/IR/AffineMap.h"
#include "mlir/IR/PatternMatch.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// narval.tensor_map → tensor.empty + linalg.generic
//===----------------------------------------------------------------------===//

struct LowerTensorMap : public OpRewritePattern<TensorMapOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorMapOp op,
                                  PatternRewriter& r) const override {
        auto res_type = dyn_cast<RankedTensorType>(op.getResult().getType());
        if (!res_type) return failure();

        auto loc = op.getLoc();
        auto empty = make_empty_tensor(r, loc, res_type);
        auto map = AffineMap::getMultiDimIdentityMap(res_type.getRank(),
                                                     r.getContext());
        SmallVector<AffineMap> maps = {map, map};
        SmallVector<utils::IteratorType> iterators(
            res_type.getRank(), utils::IteratorType::parallel);

        auto generic = linalg::GenericOp::create(
            r, loc, TypeRange{res_type}, ValueRange{op.getInput()},
            ValueRange{empty}, maps, iterators,
            [&](OpBuilder& body_builder, Location body_loc, ValueRange args) {
                if (failed(inline_linalg_body(
                        op.getMapper(), body_builder, body_loc, args.take_front(1))))
                    linalg::YieldOp::create(body_builder, body_loc, args.front());
            });

        r.replaceOp(op, generic.getResults());
        return success();
    }
};

} // namespace

void populateLowerTensorMap(RewritePatternSet& patterns, MLIRContext* ctx) {
    patterns.add<LowerTensorMap>(ctx);
}

} // namespace nv
