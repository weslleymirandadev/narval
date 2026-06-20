#include "backend/nir/NarvalOps.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

struct LowerReturnOp : public OpConversionPattern<ReturnOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(ReturnOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOpWithNewOp<func::ReturnOp>(op, a.getOperands());
        return success();
    }
};

} // namespace

void populateLowerReturnOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerReturnOp>(tc, patterns.getContext());
}

} // namespace nv
