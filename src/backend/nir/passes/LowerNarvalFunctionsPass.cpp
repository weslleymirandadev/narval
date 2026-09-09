#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/passes/NarvalTypeConverter.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#define GEN_PASS_DEF_LOWERNARVALFUNCTIONSPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

//===----------------------------------------------------------------------===//
// narval.return → func.return conversion
//===----------------------------------------------------------------------===//

struct ConvertReturnOp : public OpConversionPattern<ReturnOp> {
    using OpConversionPattern::OpConversionPattern;

    LogicalResult matchAndRewrite(
        ReturnOp op, OpAdaptor adaptor,
        ConversionPatternRewriter& rewriter) const override {
        rewriter.replaceOpWithNewOp<func::ReturnOp>(op, adaptor.getOperands());
        return success();
    }
};

//===----------------------------------------------------------------------===//
// Lower narval.func → func.func (converting body ops along the way)
//===----------------------------------------------------------------------===//

static void lower_func(FuncOp op) {
    OpBuilder builder(op);
    MLIRContext* ctx = op.getContext();

    // Convert the narval-typed signature (!narval.value) to the runtime ABI
    // type (!llvm.ptr) up front and retype the entry block arguments to match.
    // If entry args stayed !narval.value, every later use of a parameter would
    // need a !narval.value → !llvm.ptr materialization that the conversion
    // framework cannot absorb (the defining "op" of a block arg is never
    // converted) → "failed to legalize unresolved materialization".
    NarvalTypeConverter tc(ctx);
    SmallVector<Type> param_types;
    for (Type t : op.getFunctionType().getInputs()) {
        Type converted = tc.convertType(t);
        param_types.push_back(converted ? converted : t);
    }
    SmallVector<Type> ret_types;
    for (Type t : op.getFunctionType().getResults()) {
        Type converted = tc.convertType(t);
        ret_types.push_back(converted ? converted : t);
    }
    auto fn_type = FunctionType::get(ctx, param_types, ret_types);

    auto func = func::FuncOp::create(builder, op.getLoc(), op.getSymName(),
                                     fn_type);
    func.setPublic();

    if (!op.getAbi().empty())
        func->setAttr("narval.abi", builder.getStringAttr(op.getAbi()));
    if (op.getIsFallible())
        func->setAttr("narval.is_fallible", builder.getUnitAttr());
    if (op.getIsAsync())
        func->setAttr("narval.is_async", builder.getUnitAttr());
    if (op.getIsComptime())
        func->setAttr("narval.is_comptime", builder.getUnitAttr());
    if (auto optimize = op->getAttr("narval.optimize"))
        func->setAttr("narval.optimize", optimize);

    // Move the body from narval.func to func.func
    func.getBody().takeBody(op.getBody());

    // Retype the entry block arguments to the converted parameter types.
    if (!func.getBody().empty()) {
        Block& entry = func.getBody().front();
        for (size_t i = 0; i < entry.getNumArguments() && i < param_types.size();
             ++i)
            entry.getArgument(i).setType(param_types[i]);
    }

    // Convert narval.return → func.return inside the new func.func body
    ConversionTarget target(*op.getContext());
    target.addIllegalOp<ReturnOp>();
    target.addLegalOp<func::ReturnOp>();

    RewritePatternSet patterns(op.getContext());
    patterns.add<ConvertReturnOp>(op.getContext());

    if (failed(applyPartialConversion(func.getOperation(), target,
                                       std::move(patterns)))) {
        op->emitError("failed to lower narval.return ops");
    }

    op.erase();
}

struct LowerNarvalFunctionsPassImpl
    : public ::impl::LowerNarvalFunctionsPassBase<
          LowerNarvalFunctionsPassImpl> {

    void runOnOperation() override {
        SmallVector<FuncOp> funcs;
        getOperation().walk([&](FuncOp op) { funcs.push_back(op); });
        for (FuncOp op : funcs)
            lower_func(op);
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerNarvalFunctionsPass() {
    return std::make_unique<LowerNarvalFunctionsPassImpl>();
}

} // namespace nv
