#include "backend/nir/passes/NarvalTypeConverter.hpp"
#include "backend/nir/NarvalOps.h"

#include "mlir/Dialect/SCF/IR/SCF.h"

using namespace mlir;

namespace nv {
namespace {

// scf.if whose results are still !narval.value. The whole scf dialect is legal for
// this pass, so the driver converts the region bodies (narval ops → func.call
// returning !llvm.ptr) but keeps the op itself and inserts a ptr→value cast for the
// yields — a cast no legal op can absorb, so the conversion aborts with "failed to
// legalize unresolved materialization". Rebuilding the op with converted results and
// moving the regions in keeps the values in !llvm.ptr from producer to consumer.
struct LowerSCFIfOp : public OpConversionPattern<scf::IfOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(scf::IfOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        SmallVector<Type> result_types;
        if (failed(getTypeConverter()->convertTypes(op.getResultTypes(),
                                                    result_types)))
            return failure();

        bool has_else = !op.getElseRegion().empty();

        // scf.if pre-populates then (and optional else) with one empty block each.
        auto new_if = scf::IfOp::create(r, op.getLoc(), result_types,
                                        adaptor.getCondition(), has_else);
        {
            Block& placeholder = new_if.getThenRegion().front();
            r.inlineRegionBefore(op.getThenRegion(), new_if.getThenRegion(),
                                 new_if.getThenRegion().begin());
            r.eraseBlock(&placeholder);
        }
        if (has_else) {
            Block& placeholder = new_if.getElseRegion().front();
            r.inlineRegionBefore(op.getElseRegion(), new_if.getElseRegion(),
                                 new_if.getElseRegion().begin());
            r.eraseBlock(&placeholder);
        }

        r.replaceOp(op, new_if.getResults());
        return success();
    }
};

// The yield of a converted scf.if: its operands have to be the converted values,
// otherwise the terminator still writes !narval.value into a !llvm.ptr result.
// Yields of any other parent (scf.while/scf.for regions) belong to their own op's
// types and are left alone.
struct LowerSCFYieldOp : public OpConversionPattern<scf::YieldOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(scf::YieldOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        if (!isa_and_nonnull<scf::IfOp>(op->getParentOp()))
            return failure();
        r.replaceOpWithNewOp<scf::YieldOp>(op, adaptor.getOperands());
        return success();
    }
};

} // namespace

void populateLowerSCFIfOp(RewritePatternSet& patterns,
                          mlir::narval::NarvalTypeConverter& tc) {
    patterns.add<LowerSCFIfOp, LowerSCFYieldOp>(tc, patterns.getContext());
}

} // namespace nv
