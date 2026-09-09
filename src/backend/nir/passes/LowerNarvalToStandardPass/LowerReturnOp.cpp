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

// narval.func lowering retypes user function signatures to !llvm.ptr, so a
// func.return may still carry !narval.value operands whose producers get
// converted later in this pass. func.return is dynamically illegal in that
// case; this pattern rewrites the operand to its converted (ptr) value so the
// conversion framework doesn't leave an unrealized cast (ptr → value) behind.
struct LowerFuncReturnOp : public OpConversionPattern<func::ReturnOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(func::ReturnOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOpWithNewOp<func::ReturnOp>(op, a.getOperands());
        return success();
    }
};

} // namespace

void populateLowerReturnOp(RewritePatternSet& patterns, mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerReturnOp>(tc, patterns.getContext());
    patterns.add<LowerFuncReturnOp>(tc, patterns.getContext());
}

} // namespace nv
