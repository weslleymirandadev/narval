#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#define GEN_PASS_DEF_LOWERNARVALCONTROLFLOWPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// Pass-through type converter
//===----------------------------------------------------------------------===//

class CFTypeConverter : public TypeConverter {
public:
    explicit CFTypeConverter(MLIRContext*) {
        addConversion([](Type t) { return t; });
    }
};

//===----------------------------------------------------------------------===//
// narval.yield → scf.yield
//===----------------------------------------------------------------------===//

struct LowerYieldOp : public OpConversionPattern<YieldOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(YieldOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOpWithNewOp<scf::YieldOp>(op, adaptor.getOperands());
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.if → scf.if
//
// Strategy: create scf.if, then move narval's then/else blocks in front of
// scf.if's placeholder blocks, and erase the placeholders.
//===----------------------------------------------------------------------===//

struct LowerIfOp : public OpConversionPattern<IfOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(IfOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        SmallVector<Type> result_types;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), result_types)))
            return failure();

        bool has_else = !op.getElseRegion().empty() &&
                        !op.getElseRegion().front().empty();

        // Create scf.if — it pre-populates then (and optional else) regions with
        // a single empty block each.
        auto scf_if = scf::IfOp::create(r, op.getLoc(), result_types,
                                         adaptor.getCondition(), has_else);

        // Inline narval.if then region before scf.if's then region placeholder.
        {
            Block& placeholder = scf_if.getThenRegion().front();
            r.inlineRegionBefore(op.getThenRegion(), scf_if.getThenRegion(),
                                  scf_if.getThenRegion().begin());
            r.eraseBlock(&placeholder);
        }

        if (has_else) {
            Block& placeholder = scf_if.getElseRegion().front();
            r.inlineRegionBefore(op.getElseRegion(), scf_if.getElseRegion(),
                                  scf_if.getElseRegion().begin());
            r.eraseBlock(&placeholder);
        }

        r.replaceOp(op, scf_if.getResults());
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.for_range → scf.for
//
// scf.for body has a single block with args: (index, iter_arg0, iter_arg1, ...).
// narval.for_range body must be created with the same block signature.
// We merge narval's body block into scf.for's body block.
//===----------------------------------------------------------------------===//

struct LowerForRangeOp : public OpConversionPattern<ForRangeOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(ForRangeOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        SmallVector<Type> result_types;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), result_types)))
            return failure();

        auto scf_for = scf::ForOp::create(r, op.getLoc(),
            adaptor.getLb(), adaptor.getUb(), adaptor.getStep(),
            adaptor.getInitArgs());

        // Move narval body blocks into scf.for's body region.
        // scf.for creates one body block with (index, iter_args...) args.
        // narval.for_range's body should have the same block arg structure.
        // Use mergeBlocks to merge the narval body block into the scf body block.
        Region& narval_body = op.getBody();
        if (!narval_body.empty() && !narval_body.front().empty()) {
            Block* scf_body = scf_for.getBody();    // scf.for's body block
            Block& narval_block = narval_body.front();
            // Replace narval block args (if any) with scf.for's block args, then merge.
            r.mergeBlocks(&narval_block, scf_body, scf_body->getArguments());
        }

        r.replaceOp(op, scf_for.getResults());
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.while → scf.while
//===----------------------------------------------------------------------===//

struct LowerWhileOp : public OpConversionPattern<WhileOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(WhileOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        SmallVector<Type> result_types;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), result_types)))
            return failure();

        auto scf_while = scf::WhileOp::create(r, op.getLoc(), result_types,
                                               adaptor.getInitArgs());

        // Move condition region → scf.while before-region.
        {
            Block& placeholder = scf_while.getBefore().front();
            r.inlineRegionBefore(op.getConditionRegion(), scf_while.getBefore(),
                                  scf_while.getBefore().begin());
            r.eraseBlock(&placeholder);
        }

        // Move body region → scf.while after-region.
        {
            Block& placeholder = scf_while.getAfter().front();
            r.inlineRegionBefore(op.getBodyRegion(), scf_while.getAfter(),
                                  scf_while.getAfter().begin());
            r.eraseBlock(&placeholder);
        }

        r.replaceOp(op, scf_while.getResults());
        return success();
    }
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct LowerNarvalControlFlowPassImpl
    : public ::impl::LowerNarvalControlFlowPassBase<
          LowerNarvalControlFlowPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        MLIRContext* ctx = &getContext();

        CFTypeConverter tc(ctx);
        ConversionTarget target(*ctx);

        target.addLegalDialect<scf::SCFDialect, func::FuncDialect,
                               arith::ArithDialect>();
        target.addLegalOp<ModuleOp>();
        target.addIllegalOp<IfOp, ForRangeOp, WhileOp, YieldOp>();
        // Leave all other narval.* alone for the standard pass.
        target.markUnknownOpDynamicallyLegal([](Operation*) { return true; });

        RewritePatternSet patterns(ctx);
        patterns.add<LowerYieldOp, LowerIfOp, LowerForRangeOp, LowerWhileOp>(
            tc, ctx);

        if (failed(applyPartialConversion(module, target, std::move(patterns)))) {
            module.emitError("lower-narval-cf: partial conversion failed");
            signalPassFailure();
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerNarvalControlFlowPass() {
    return std::make_unique<LowerNarvalControlFlowPassImpl>();
}

} // namespace nv
