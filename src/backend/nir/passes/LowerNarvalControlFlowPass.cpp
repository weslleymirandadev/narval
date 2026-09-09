#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
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

    // True when the block's terminator is an (early) function return.
    static bool returnsFromBlock(Block* block) {
        if (block->empty()) return false;
        Operation* term = &block->back();
        return isa<func::ReturnOp, narval::ReturnOp>(term);
    }

    static bool isYieldTerminator(Operation* term) {
        return isa<narval::YieldOp, scf::YieldOp>(term);
    }

    static bool isLoopExitTerminator(Operation* term) {
        return isa<narval::BreakOp, narval::ContinueOp>(term);
    }

    // True when the block ends in a bare yield (fall-through) that must be
    // replaced by a branch; return/break/continue terminators are kept as-is
    // (the enclosing while/for lowering resolves them).
    static bool endsWithFallthroughYield(Block* block) {
        return !block->empty() && isYieldTerminator(&block->back());
    }

    // Statement if (no results): lower to a cf.cond_br CFG directly.
    // scf.if regions that end in func.return (early return in one branch) are
    // invalid: later region DCE (ConvertVectorToLLVMPass) crashes on them.
    LogicalResult lowerStatementIf(IfOp op, OpAdaptor adaptor,
                                   ConversionPatternRewriter& r) const {
        Location loc = op.getLoc();
        Block* parent = op->getBlock();
        if (!parent) return failure();

        // Split: everything after the if goes to a continuation block.
        Block* cont = r.splitBlock(parent, op->getIterator());

        // Build then/else blocks between parent and cont.
        r.setInsertionPointToEnd(parent);
        Block* thenBlk = r.createBlock(cont);
        Block* elseBlk = r.createBlock(cont);

        // Move then-region ops into thenBlk.
        Block* thenSrc = &op.getThenRegion().front();
        r.mergeBlocks(thenSrc, thenBlk, {});
        if (endsWithFallthroughYield(thenBlk)) {
            // Fall-through — branch to cont; other terminators (return/break/
            // continue) are kept for the enclosing loop lowering.
            Operation* term = &thenBlk->back();
            r.eraseOp(term);
            r.setInsertionPointToEnd(thenBlk);
            r.create<cf::BranchOp>(loc, cont);
        }

        // Move else-region ops into elseBlk (empty else = just br cont).
        bool hasElse = !op.getElseRegion().empty() &&
                       !op.getElseRegion().front().empty();
        if (hasElse) {
            Block* elseSrc = &op.getElseRegion().front();
            // A region containing only a yield is a vacuous else.
            bool vacuousElse = llvm::hasSingleElement(elseSrc->getOperations()) &&
                               isYieldTerminator(&elseSrc->back());
            if (vacuousElse) {
                hasElse = false;
            } else {
                r.mergeBlocks(elseSrc, elseBlk, {});
                if (endsWithFallthroughYield(elseBlk)) {
                    Operation* term = &elseBlk->back();
                    r.eraseOp(term);
                    r.setInsertionPointToEnd(elseBlk);
                    r.create<cf::BranchOp>(loc, cont);
                }
            }
        }
        if (!hasElse) {
            r.setInsertionPointToEnd(elseBlk);
            r.create<cf::BranchOp>(loc, cont);
        }

        // Terminate parent with the conditional branch and drop the if op.
        r.setInsertionPointToEnd(parent);
        r.create<cf::CondBranchOp>(loc, adaptor.getCondition(),
                                   thenBlk, ValueRange{}, elseBlk, ValueRange{});
        r.eraseOp(op);
        return success();
    }

    LogicalResult matchAndRewrite(IfOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        // Statement-if (no results) → cf.cond_br CFG (handles early returns).
        if (op.getNumResults() == 0)
            return lowerStatementIf(op, adaptor, r);

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

    // Statement-while (no results/inits — always the case from codegen):
    // lower straight to a cf.cond_br CFG. scf.while proved unreliable here
    // (region-block juggling crashed both inline+eraseBlock and the walk in
    // the next pass), and a CFG also permits multi-block bodies produced by
    // nested statement-ifs and early returns.
    LogicalResult lowerStatementWhile(WhileOp op, OpAdaptor adaptor,
                                      ConversionPatternRewriter& r) const {
        Location loc = op.getLoc();
        Block* parent = op->getBlock();
        if (!parent) return failure();

        // Everything after the while moves to a continuation block.
        Block* cont = r.splitBlock(parent, op->getIterator());

        // Loop header (condition) block, before cont.
        Block* loop_blk = r.createBlock(cont);

        // Resolve narval.break/continue ANYWHERE inside the while's regions
        // (they may sit inside not-yet-lowered if regions): break → br cont,
        // continue → br loop_blk. The LowerIfOp runs after this pattern and
        // keeps these branch terminators when it moves blocks around.
        llvm::SmallVector<Operation*> exits;
        op.getConditionRegion().walk([&](Operation* o) {
            if (isa<narval::BreakOp, narval::ContinueOp>(o))
                exits.push_back(o);
        });
        op.getBodyRegion().walk([&](Operation* o) {
            if (isa<narval::BreakOp, narval::ContinueOp>(o))
                exits.push_back(o);
        });
        for (Operation* e : exits) {
            r.setInsertionPoint(e);
            if (isa<narval::BreakOp>(e))
                r.create<cf::BranchOp>(loc, cont);
            else
                r.create<cf::BranchOp>(loc, loop_blk);
            r.eraseOp(e);
        }

        // Move ALL body blocks into the function region, before cont.
        Region& fn_region = *cont->getParent();
        llvm::SmallVector<Block*> body_blocks;
        for (Block& b : op.getBodyRegion()) body_blocks.push_back(&b);
        Block* body_first = body_blocks.empty() ? nullptr : body_blocks.front();
        if (body_first)
            r.inlineRegionBefore(op.getBodyRegion(), fn_region,
                                 cont->getIterator());

        // Move condition ops into loop_blk; extract the i1 from its
        // terminating yield.
        if (!op.getConditionRegion().empty()) {
            Block* cond_src = &op.getConditionRegion().front();
            r.mergeBlocks(cond_src, loop_blk, {});
        }
        Value cond;
        if (!loop_blk->empty()) {
            Operation* term = &loop_blk->back();
            if (isa<narval::YieldOp, scf::YieldOp>(term) &&
                term->getNumOperands() >= 1) {
                cond = term->getOperand(0);
                r.eraseOp(term);
            }
        }
        if (!cond)
            cond = arith::ConstantIntOp::create(r, loc, 0, 1).getResult();

        // Terminate loop_blk with the conditional branch.
        r.setInsertionPointToEnd(loop_blk);
        r.create<cf::CondBranchOp>(loc, cond,
                                   body_first ? body_first : cont,
                                   ValueRange{}, cont, ValueRange{});

        // Body fall-through blocks (yield terminators) branch back to the
        // condition; func.return stays as an early exit.
        for (Block* bb : body_blocks) {
            if (bb->empty()) continue;
            Operation* term = &bb->back();
            if (isa<narval::YieldOp, scf::YieldOp>(term)) {
                r.eraseOp(term);
                r.setInsertionPointToEnd(bb);
                r.create<cf::BranchOp>(loc, loop_blk);
            }
        }

        // Parent falls through into the loop header.
        r.setInsertionPointToEnd(parent);
        r.create<cf::BranchOp>(loc, loop_blk);

        r.eraseOp(op);
        return success();
    }

    LogicalResult matchAndRewrite(WhileOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        if (op.getNumResults() == 0 && op.getInitArgs().empty())
            return lowerStatementWhile(op, adaptor, r);

        SmallVector<Type> result_types;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), result_types)))
            return failure();

        auto scf_while = scf::WhileOp::create(r, op.getLoc(), result_types,
                                               adaptor.getInitArgs());

        // Move condition region → scf.while before-region (merge into the
        // placeholder block — inline+eraseBlock crashed in this pass).
        {
            Block* scf_before = &scf_while.getBefore().front();
            if (!op.getConditionRegion().empty())
                r.mergeBlocks(&op.getConditionRegion().front(), scf_before,
                              scf_before->getArguments());
        }

        // Move body region → scf.while after-region.
        {
            Block* scf_after = &scf_while.getAfter().front();
            if (!op.getBodyRegion().empty())
                r.mergeBlocks(&op.getBodyRegion().front(), scf_after,
                              scf_after->getArguments());
        }

        // The codegen terminates the condition region with a yield carrying the
        // i1 (narval.yield{i1}). scf.while requires its before-region to end
        // with scf.condition.
        Block& before_blk = scf_while.getBefore().front();
        if (!before_blk.empty()) {
            Operation* term = &before_blk.back();
            if (isa<narval::YieldOp, scf::YieldOp>(term) &&
                term->getNumOperands() >= 1) {
                Value cond = term->getOperand(0);
                r.eraseOp(term);
                r.setInsertionPointToEnd(&before_blk);
                r.create<scf::ConditionOp>(op.getLoc(), cond, ValueRange{});
            }
        }

        // The after-region must end with an (empty) scf.yield.
        Block& after_blk = scf_while.getAfter().front();
        if (!after_blk.empty() && isa<narval::YieldOp>(&after_blk.back())) {
            Operation* term = &after_blk.back();
            r.eraseOp(term);
            r.setInsertionPointToEnd(&after_blk);
            r.create<scf::YieldOp>(op.getLoc(), ValueRange{});
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
        target.addIllegalOp<IfOp, ForRangeOp, WhileOp, YieldOp, BreakOp,
                            ContinueOp>();
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
