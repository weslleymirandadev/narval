#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace nv {
namespace {

// cf.br/cf.cond_br are dynamically illegal while they carry !narval.value
// operands (loop-carried CFG produced by the while lowering). Their producer
// ops may be converted before or after the branch is visited, so these
// patterns rewrite the branch with the adapted (converted) operands, letting
// the framework materialize the vt→ptr conversions exactly like
// LowerFuncReturnOp does for func.return.
struct LowerCFBranchOp : public OpConversionPattern<cf::BranchOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(cf::BranchOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOpWithNewOp<cf::BranchOp>(op, op.getDest(),
                                            a.getOperands());
        return success();
    }
};

struct LowerCFCondBranchOp : public OpConversionPattern<cf::CondBranchOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(cf::CondBranchOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOpWithNewOp<cf::CondBranchOp>(
            op, a.getCondition(), op.getTrueDest(),
            a.getTrueDestOperands(), op.getFalseDest(),
            a.getFalseDestOperands());
        return success();
    }
};

} // namespace

void populateLowerCFBranchOp(RewritePatternSet& patterns,
                             mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerCFBranchOp, LowerCFCondBranchOp>(
        tc, patterns.getContext());
}

} // namespace nv
