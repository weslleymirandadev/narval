#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
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

    // for_range lowers to the same cf.cond_br CFG as whiles (NOT scf.for):
    // scf.for with carried values hits the same conversion walls as
    // scf.while — block-arg vt types, yields, and materialization casts for
    // values entering a legal op. The CFG carries the index and the
    // loop-carried values through block args (codegen body block args are
    // index + carried, retyped to !llvm.ptr for carried), and branch values
    // flow in as vt until lower-narval-to-std converts their producers
    // (cf.br/cf.cond_br are dynamically legal there — see LowerCFBranch.cpp).
    LogicalResult matchAndRewrite(ForRangeOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        Location loc = op.getLoc();
        Block* parent = op->getBlock();
        if (!parent) return failure();

        bool has_carried = !op.getInitArgs().empty();
        NarvalTypeConverter ntc(r.getContext());
        SmallVector<Type> carried_types;
        for (Type t : op.getInitArgs().getTypes()) {
            Type c = ntc.convertType(t);
            carried_types.push_back(c ? c : t);
        }
        auto idx_ty = r.getIndexType();

        // Everything after the loop moves to a continuation block that
        // receives the final carried values as results.
        Block* cont = r.splitBlock(parent, op->getIterator());
        llvm::SmallVector<Location> locs;
        if (has_carried) {
            locs.assign(carried_types.size(), loc);
            cont->addArguments(carried_types, locs);
        }

        // Loop header (condition) block: (index, carried...).
        Block* loop_blk = r.createBlock(cont);
        {
            llvm::SmallVector<Type> hdr_types;
            hdr_types.push_back(idx_ty);
            for (Type t : carried_types) hdr_types.push_back(t);
            llvm::SmallVector<Location> hdr_locs(hdr_types.size(), loc);
            loop_blk->addArguments(hdr_types, hdr_locs);
        }

        // Move ALL body blocks into the function region, before cont.
        Region& fn_region = *cont->getParent();
        llvm::SmallVector<Block*> body_blocks;
        for (Block& b : op.getBody()) body_blocks.push_back(&b);
        Block* body_first = body_blocks.empty() ? nullptr : body_blocks.front();
        if (body_first) {
            r.inlineRegionBefore(op.getBody(), fn_region, cont->getIterator());
            // Retype the carried block args (index stays index).
            if (has_carried) {
                size_t n = body_first->getNumArguments() > 1
                               ? body_first->getNumArguments() - 1
                               : 0;
                if (n > carried_types.size()) n = carried_types.size();
                for (size_t i = 0; i < n; ++i)
                    body_first->getArgument(1 + i).setType(carried_types[i]);
            }
        }

        // Resolve break/continue inside the body (they may sit inside
        // not-yet-lowered if regions): break → cont (carried only); continue →
        // the header WITH the index incremented (a plain branch to the header
        // would skip the increment and loop forever). Limitation: carries the
        // iteration-entry values (same rule as while).
        ValueRange cur_vals = (body_first && has_carried)
                                  ? ValueRange(body_first->getArguments())
                                         .drop_front()
                                  : ValueRange{};
        llvm::SmallVector<Operation*> exits;
        for (Block* bb : body_blocks)
            bb->walk([&](Operation* o) {
                if (isa<narval::BreakOp, narval::ContinueOp>(o))
                    exits.push_back(o);
            });
        for (Operation* e : exits) {
            r.setInsertionPoint(e);
            if (isa<narval::BreakOp>(e)) {
                r.create<cf::BranchOp>(loc, cont, cur_vals);
            } else {
                llvm::SmallVector<Value> cont_vals;
                if (body_first) {
                    Value next = r
                        .create<arith::AddIOp>(
                            loc, body_first->getArgument(0),
                            adaptor.getStep())
                        .getResult();
                    cont_vals.push_back(next);
                    for (Value v : cur_vals) cont_vals.push_back(v);
                }
                r.create<cf::BranchOp>(loc, loop_blk, cont_vals);
            }
            r.eraseOp(e);
        }

        // Condition ops into loop_blk: index < ub.
        r.setInsertionPointToEnd(loop_blk);
        Value ub = adaptor.getUb();
        Value cond =
            r.create<arith::CmpIOp>(loc, arith::CmpIPredicate::slt,
                                    loop_blk->getArgument(0), ub)
                .getResult();
        r.create<cf::CondBranchOp>(loc, cond,
                                   body_first ? body_first : cont,
                                   loop_blk->getArguments(),
                                   cont,
                                   has_carried
                                       ? ValueRange(loop_blk->getArguments())
                                             .drop_front()
                                       : ValueRange{});

        // Body fall-through: replace the terminating yield with the
        // increment + back edge (index' = index + step, carried' = yield
        // values). func.return stays as an early exit.
        if (body_first && !body_first->empty()) {
            Operation* term = &body_first->back();
            if (isa<narval::YieldOp, scf::YieldOp>(term)) {
                llvm::SmallVector<Value> yield_vals;
                for (Value v : term->getOperands()) yield_vals.push_back(v);
                r.eraseOp(term);
                r.setInsertionPointToEnd(body_first);
                Value idx = body_first->getArgument(0);
                Value step = adaptor.getStep();
                Value next = r.create<arith::AddIOp>(loc, idx, step).getResult();
                llvm::SmallVector<Value> back;
                back.push_back(next);
                for (Value v : yield_vals) back.push_back(v);
                r.create<cf::BranchOp>(loc, loop_blk, back);
            }
        }

        // Parent falls through into the header: index = lb, carried = inits.
        r.setInsertionPointToEnd(parent);
        llvm::SmallVector<Value> entry;
        entry.push_back(adaptor.getLb());
        for (Value v : adaptor.getInitArgs()) entry.push_back(v);
        r.create<cf::BranchOp>(loc, loop_blk, entry);

        // Replace results by hand (codegen types vt differ from the block-arg
        // ptr types; r.replaceOp would make the framework materialize).
        if (has_carried) {
            for (auto [res, arg] : llvm::zip(op.getResults(),
                                             cont->getArguments()))
                res.replaceAllUsesWith(arg);
        }
        r.eraseOp(op);
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.while → scf.while
//===----------------------------------------------------------------------===//

struct LowerWhileOp : public OpConversionPattern<WhileOp> {
    using OpConversionPattern::OpConversionPattern;

    // Statement-whiles (and loop-carried while loops) lower straight to a
    // cf.cond_br CFG. scf.while proved unreliable here (region-block juggling
    // crashed both inline+eraseBlock and the walk in the next pass), and a CFG
    // also permits multi-block bodies produced by nested statement-ifs, early
    // returns, and break/continue.
    //
    // Loop-carried values flow through block arguments using the runtime ABI
    // type (!llvm.ptr): condition/body region block args (vt from codegen) are
    // remapped onto ptr-typed args by mergeBlocks; values produced by body ops
    // (vt, results of runtime calls) are materialized into the back-edge with
    // unrealized_conversion_cast vt→ptr, which later passes collapse once the
    // producer ops are converted.
    LogicalResult matchAndRewrite(WhileOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        Location loc = op.getLoc();
        auto* ctx = r.getContext();
        Block* parent = op->getBlock();
        if (!parent) return failure();

        bool has_carried = !op.getInitArgs().empty();
        // The CF pass type converter is identity, so carried block args are
        // retyped to the runtime ABI type explicitly (like
        // LowerNarvalFunctionsPass — the conversion framework cannot convert
        // block-argument types). Values entering the blocks (init args, body
        // yields) are vt results of ops; they flow into the branches
        // unconverted, and lower-narval-to-std converts the producer ops and
        // re-legalizes the branches (cf.br/cf.cond_br are dynamically legal
        // there), so everything lines up as !llvm.ptr without any
        // materialization casts.
        NarvalTypeConverter ntc(ctx);
        SmallVector<Type> carried_types;
        for (Type t : op.getInitArgs().getTypes()) {
            Type c = ntc.convertType(t);
            carried_types.push_back(c ? c : t);
        }

        // Everything after the while moves to a continuation block, which
        // receives the final carried values as its (result) arguments.
        Block* cont = r.splitBlock(parent, op->getIterator());
        llvm::SmallVector<Location> locs;
        if (has_carried) {
            locs.assign(carried_types.size(), loc);
            cont->addArguments(carried_types, locs);
        }

        // Loop header (condition) block, before cont, holding the live values.
        Block* loop_blk = r.createBlock(cont);
        if (has_carried) loop_blk->addArguments(carried_types, locs);

        // Move ALL body blocks into the function region, before cont.
        Region& fn_region = *cont->getParent();
        llvm::SmallVector<Block*> body_blocks;
        for (Block& b : op.getBodyRegion()) body_blocks.push_back(&b);
        Block* body_first = body_blocks.empty() ? nullptr : body_blocks.front();
        if (body_first)
            r.inlineRegionBefore(op.getBodyRegion(), fn_region,
                                 cont->getIterator());

        // The body's front block keeps its codegen'd args (vt) — retype them
        // to the runtime ABI type now (block-arg types cannot be converted by
        // the framework; LowerNarvalFunctionsPass does the same for entry
        // args).
        if (body_first && has_carried) {
            size_t n = body_first->getNumArguments();
            if (n > carried_types.size()) n = carried_types.size();
            for (size_t i = 0; i < n; ++i)
                body_first->getArgument(i).setType(carried_types[i]);
        }

        // Resolve narval.break/continue ANYWHERE inside the while's regions
        // (they may sit inside not-yet-lowered if regions): break → br cont,
        // continue → br loop_blk, both carrying the current iteration values
        // (the body's front-block args — correct when the exit precedes any
        // reassignment in the body, the typical `while (true) { if (c) break;
        // ... }` pattern). The LowerIfOp keeps these branch terminators when
        // it moves blocks around.
        ValueRange cur_vals = (body_first && has_carried)
                                  ? ValueRange(body_first->getArguments())
                                  : ValueRange{};
        llvm::SmallVector<Operation*> exits;
        op.getConditionRegion().walk([&](Operation* o) {
            if (isa<narval::BreakOp, narval::ContinueOp>(o))
                exits.push_back(o);
        });
        for (Block* bb : body_blocks)
            bb->walk([&](Operation* o) {
                if (isa<narval::BreakOp, narval::ContinueOp>(o))
                    exits.push_back(o);
            });
        for (Operation* e : exits) {
            r.setInsertionPoint(e);
            if (isa<narval::BreakOp>(e))
                r.create<cf::BranchOp>(loc, cont, cur_vals);
            else
                r.create<cf::BranchOp>(loc, loop_blk, cur_vals);
            r.eraseOp(e);
        }

        // Move condition ops into loop_blk, remapping the condition region's
        // block args (the loop-carried values) onto loop_blk's args. Done
        // manually (splice + direct replaceAllUsesWith) instead of
        // mergeBlocks: mergeBlocks notifies the conversion driver of the
        // vt→ptr arg replacement, which makes it materialize a ptr→vt cast for
        // the remaining vt uses → "unresolved materialization" failure.
        if (!op.getConditionRegion().empty()) {
            Block* cond_src = &op.getConditionRegion().front();
            loop_blk->getOperations().splice(loop_blk->end(),
                                             cond_src->getOperations());
            for (auto [src_arg, dst_arg] :
                 llvm::zip(cond_src->getArguments(),
                           loop_blk->getArguments())) {
                if (src_arg.getType() != dst_arg.getType())
                    src_arg.replaceAllUsesWith(dst_arg);
            }
            r.eraseBlock(cond_src);
        }

        // Extract the i1 from the condition's terminating yield
        // (yield {i1, carried...}).
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

        // Terminate loop_blk: true → body (carried stay live), false → cont
        // (carried are the final results).
        r.setInsertionPointToEnd(loop_blk);
        r.create<cf::CondBranchOp>(loc, cond,
                                   body_first ? body_first : cont,
                                   loop_blk->getArguments(),
                                   cont, loop_blk->getArguments());

        // Body fall-through blocks: yield {updated values} → branch back to
        // the header carrying the updated values; func.return stays as an
        // early exit. Values are vt (results of runtime calls) — materialize
        // vt→ptr casts that later passes collapse after converting producers.
        for (Block* bb : body_blocks) {
            if (bb->empty()) continue;
            Operation* term = &bb->back();
            if (isa<narval::YieldOp, scf::YieldOp>(term)) {
                llvm::SmallVector<Value> vals;
                for (Value v : term->getOperands()) vals.push_back(v);
                r.eraseOp(term);
                r.setInsertionPointToEnd(bb);
                r.create<cf::BranchOp>(loc, loop_blk, vals);
            }
        }

        // Parent falls through into the loop header with the initial values.
        r.setInsertionPointToEnd(parent);
        r.create<cf::BranchOp>(loc, loop_blk, adaptor.getInitArgs());

        // Replace the while's results with the continuation block args by
        // hand: the codegen types (vt) differ from the block-arg types (ptr),
        // and r.replaceOp would ask the framework to materialize ptr→vt (the
        // CF type converter cannot). Uses of the vt results (later runtime
        // calls) tolerate ptr operands — lower-narval-to-std converts them.
        if (has_carried) {
            for (auto [res, arg] :
                 llvm::zip(op.getResults(), cont->getArguments()))
                res.replaceAllUsesWith(arg);
            r.eraseOp(op);
        } else {
            r.eraseOp(op);
        }
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
