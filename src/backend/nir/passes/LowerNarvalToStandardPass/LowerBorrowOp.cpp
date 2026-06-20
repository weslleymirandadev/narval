#include "backend/nir/NarvalOps.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

struct LowerBorrowOp : public OpConversionPattern<BorrowOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(BorrowOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOp(op, a.getSource()); return success();
    }
};

} // namespace

void populateLowerBorrowOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerBorrowOp>(tc, patterns.getContext());
}

} // namespace nv
