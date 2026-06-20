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
// narval.tensor_reduce → linalg.reduce
//===----------------------------------------------------------------------===//

struct LowerTensorReduce : public OpRewritePattern<TensorReduceOp> {
    using OpRewritePattern::OpRewritePattern;

    LogicalResult matchAndRewrite(TensorReduceOp op,
                                  PatternRewriter& r) const override {
        int64_t dim = static_cast<int64_t>(op.getDimension());
        auto reduce = linalg::ReduceOp::create(
            r, op.getLoc(), ValueRange{op.getInput()}, ValueRange{op.getInit()},
            ArrayRef<int64_t>{dim},
            [&](OpBuilder& body_builder, Location body_loc, ValueRange args) {
                SmallVector<Value> combiner_args;
                if (args.size() == 2) {
                    // linalg.reduce passes (element, accumulator); Narval's
                    // combiner region is specified as (accumulator, element).
                    combiner_args.push_back(args[1]);
                    combiner_args.push_back(args[0]);
                } else {
                    combiner_args.append(args.begin(), args.end());
                }
                if (failed(inline_linalg_body(
                        op.getCombiner(), body_builder, body_loc, combiner_args)))
                    linalg::YieldOp::create(body_builder, body_loc,
                                            args.size() > 1 ? args[1] : args.front());
            });
        r.replaceOp(op, reduce.getResults());
        return success();
    }
};

} // namespace

void populateLowerTensorReduce(RewritePatternSet& patterns, MLIRContext* ctx) {
    patterns.add<LowerTensorReduce>(ctx);
}

} // namespace nv
