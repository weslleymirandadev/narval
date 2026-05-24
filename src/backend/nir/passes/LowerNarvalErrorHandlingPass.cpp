// LowerNarvalErrorHandlingPass.cpp — Lower narval error ops to runtime calls.
//
// narval.throw       → nv_throw(error)           + llvm.unreachable
// narval.result_wrap → nv_make_ok(v)/nv_make_err(v)
// narval.propagate   → nv_is_result_err_i1(v) + conditional nv_throw
//                      + nv_unwrap_result(v)
// narval.try_region  → nv_try_push + scf.execute_region(try_body) +
//                      nv_try_pop + nv_had_error_i1 dispatch +
//                      scf.execute_region(finally_body)
//
// Runs after LowerNarvalControlFlowPass (so try_region bodies have scf.yield)
// and before LowerNarvalToStandardPass.

#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#define GEN_PASS_DEF_LOWERNARVALERRORHANDLINGPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

static func::FuncOp ensure_decl(ModuleOp module, OpBuilder& builder,
                                  llvm::StringRef name, FunctionType ft) {
    if (auto f = module.lookupSymbol<func::FuncOp>(name)) return f;
    OpBuilder::InsertionGuard g(builder);
    builder.setInsertionPointToStart(module.getBody());
    auto f = func::FuncOp::create(builder, builder.getUnknownLoc(), name, ft);
    f.setPrivate();
    return f;
}

// ── narval.throw → nv_throw + llvm.unreachable ───────────────────────────────

struct LowerThrowOp : public OpConversionPattern<ThrowOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(ThrowOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        ensure_decl(mod, r, "nv_throw", FunctionType::get(ctx, {ptr}, {}));
        func::CallOp::create(r, loc, TypeRange{}, "nv_throw",
                              ValueRange{a.getError()});
        r.replaceOpWithNewOp<LLVM::UnreachableOp>(op);
        return success();
    }
};

// ── narval.result_wrap → nv_make_ok / nv_make_err ────────────────────────────

struct LowerResultWrapOp : public OpConversionPattern<ResultWrapOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(ResultWrapOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        llvm::StringRef fn = op.getIsOk() ? "nv_make_ok" : "nv_make_err";
        ensure_decl(mod, r, fn, FunctionType::get(ctx, {ptr}, {ptr}));
        r.replaceOp(op,
            func::CallOp::create(r, loc, TypeRange{ptr}, fn,
                                  ValueRange{a.getValue()}).getResult(0));
        return success();
    }
};

// ── narval.propagate → err check + conditional throw + unwrap ────────────────

struct LowerPropagateOp : public OpConversionPattern<PropagateOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(PropagateOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto i32  = r.getI32Type();
        auto mod  = op->getParentOfType<ModuleOp>();
        ensure_decl(mod, r, "nv_is_result_err_i1",
                    FunctionType::get(ctx, {ptr}, {i32}));
        ensure_decl(mod, r, "nv_throw",
                    FunctionType::get(ctx, {ptr}, {}));
        ensure_decl(mod, r, "nv_unwrap_result",
                    FunctionType::get(ctx, {ptr}, {ptr}));

        auto is_err_i32 = func::CallOp::create(r, loc, TypeRange{i32},
                                                "nv_is_result_err_i1",
                                                ValueRange{a.getValue()}).getResult(0);
        auto zero   = arith::ConstantOp::create(r, loc, r.getI32IntegerAttr(0));
        auto is_err = arith::CmpIOp::create(r, loc, arith::CmpIPredicate::ne,
                                             is_err_i32, zero.getResult()).getResult();

        // if is_err: throw the value and yield (scf.if requires a yield)
        auto throw_if = scf::IfOp::create(r, loc, TypeRange{}, is_err, false);
        {
            OpBuilder::InsertionGuard g(r);
            r.setInsertionPointToStart(&throw_if.getThenRegion().front());
            func::CallOp::create(r, loc, TypeRange{}, "nv_throw",
                                  ValueRange{a.getValue()});
            scf::YieldOp::create(r, loc, ValueRange{});
        }

        // Continue path: unwrap the Ok value
        auto unwrapped = func::CallOp::create(r, loc, TypeRange{ptr},
                                               "nv_unwrap_result",
                                               ValueRange{a.getValue()}).getResult(0);
        r.replaceOp(op, unwrapped);
        return success();
    }
};

// ── narval.try_region → nv_try_push + scf.execute_region + dispatch ──────────

struct LowerTryRegionOp : public OpConversionPattern<TryRegionOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(TryRegionOp op, OpAdaptor /*a*/,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto i32  = r.getI32Type();
        auto mod  = op->getParentOfType<ModuleOp>();

        ensure_decl(mod, r, "nv_try_push",          FunctionType::get(ctx,{},{}));
        ensure_decl(mod, r, "nv_try_pop",           FunctionType::get(ctx,{},{}));
        ensure_decl(mod, r, "nv_had_error_i1",      FunctionType::get(ctx,{},{i32}));
        ensure_decl(mod, r, "nv_get_current_error", FunctionType::get(ctx,{},{ptr}));
        ensure_decl(mod, r, "nv_catch_check",       FunctionType::get(ctx,{ptr},{ptr}));
        ensure_decl(mod, r, "nv_is_truthy_i1",      FunctionType::get(ctx,{ptr},{i32}));
        ensure_decl(mod, r, "nv_clear_error",       FunctionType::get(ctx,{},{}));

        // 1. nv_try_push()
        func::CallOp::create(r, loc, TypeRange{}, "nv_try_push", ValueRange{});

        // 2. Inline try_body into scf.execute_region
        {
            auto exec = scf::ExecuteRegionOp::create(r, loc, TypeRange{});
            r.inlineRegionBefore(op.getTryBody(), exec.getRegion(),
                                  exec.getRegion().end());
        }

        // 3. nv_try_pop()
        func::CallOp::create(r, loc, TypeRange{}, "nv_try_pop", ValueRange{});

        // 4. Error dispatch
        auto had_i32 = func::CallOp::create(r, loc, TypeRange{i32},
                                             "nv_had_error_i1", ValueRange{}).getResult(0);
        auto zero    = arith::ConstantOp::create(r, loc, r.getI32IntegerAttr(0));
        auto had_i1  = arith::CmpIOp::create(r, loc, arith::CmpIPredicate::ne,
                                              had_i32, zero.getResult()).getResult();

        auto catch_arms = op.getCatchArms();
        if (!catch_arms.empty()) {
            auto catch_if = scf::IfOp::create(r, loc, TypeRange{}, had_i1, false);
            OpBuilder::InsertionGuard g(r);
            r.setInsertionPointToStart(&catch_if.getThenRegion().front());

            for (auto& arm : catch_arms) {
                // Each catch arm uses a catch-all check (nv_catch_check with null
                // type name matches any exception). When NIR codegen encodes type
                // names in arm metadata, pass the specific type string here.
                auto always_true = arith::ConstantOp::create(
                    r, loc, r.getIntegerAttr(r.getI1Type(), 1));
                auto check_i1 = always_true.getResult();

                auto arm_if = scf::IfOp::create(r, loc, TypeRange{}, check_i1, false);
                {
                    OpBuilder::InsertionGuard g2(r);
                    r.setInsertionPointToStart(&arm_if.getThenRegion().front());
                    auto exec = scf::ExecuteRegionOp::create(r, loc, TypeRange{});
                    r.inlineRegionBefore(arm, exec.getRegion(),
                                          exec.getRegion().end());
                    func::CallOp::create(r, loc, TypeRange{}, "nv_clear_error",
                                          ValueRange{});
                    scf::YieldOp::create(r, loc, ValueRange{});
                }
            }

            scf::YieldOp::create(r, loc, ValueRange{});
        }

        // 5. Inline finally_body into scf.execute_region (always runs)
        {
            auto& finally_body = op.getFinallyBody();
            if (!finally_body.empty()) {
                auto exec = scf::ExecuteRegionOp::create(r, loc, TypeRange{});
                r.inlineRegionBefore(op.getFinallyBody(), exec.getRegion(),
                                      exec.getRegion().end());
            }
        }

        r.eraseOp(op);
        return success();
    }
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct LowerNarvalErrorHandlingPassImpl
    : public ::impl::LowerNarvalErrorHandlingPassBase<
          LowerNarvalErrorHandlingPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        MLIRContext* ctx = &getContext();

        auto ptr = LLVM::LLVMPointerType::get(ctx);
        TypeConverter tc;
        tc.addConversion([ptr](narval::ValueType)  -> Type { return ptr; });
        tc.addConversion([ptr](narval::RefType)    -> Type { return ptr; });
        tc.addConversion([ptr](narval::MutRefType) -> Type { return ptr; });
        tc.addConversion([](Type t) -> std::optional<Type> {
            if (mlir::isa<narval::ValueType, narval::RefType,
                          narval::MutRefType>(t))
                return std::nullopt;
            return t;
        });

        ConversionTarget target(*ctx);
        target.addLegalDialect<func::FuncDialect, arith::ArithDialect,
                               scf::SCFDialect, LLVM::LLVMDialect>();
        target.addLegalOp<ModuleOp>();
        target.addIllegalOp<ThrowOp, ResultWrapOp, PropagateOp, TryRegionOp>();
        target.markUnknownOpDynamicallyLegal([](Operation*) { return true; });

        RewritePatternSet patterns(ctx);
        patterns.add<LowerThrowOp, LowerResultWrapOp, LowerPropagateOp,
                     LowerTryRegionOp>(tc, ctx);

        if (failed(applyPartialConversion(module, target, std::move(patterns)))) {
            module.emitError("lower-narval-error-handling: conversion failed");
            signalPassFailure();
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerNarvalErrorHandlingPass() {
    return std::make_unique<LowerNarvalErrorHandlingPassImpl>();
}

} // namespace nv
