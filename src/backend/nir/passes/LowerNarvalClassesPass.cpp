// LowerNarvalClassesPass.cpp — Lower narval object-model ops to runtime calls.
//
// narval.new         → nv_alloc_object(class_name) + __ctor_ClassName(this, args...)
// narval.get_field   → nv_get_field(obj, key_ptr)
// narval.set_field   → nv_set_field(obj, key_ptr, val)
// narval.call_method → __method_ClassName_name(this, args...)
//
// Runs before LowerNarvalToStandardPass so that the resulting func.call ops
// are picked up by the standard type-converter without any narval.* ops left.

#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

#define GEN_PASS_DEF_LOWERNARVALCLASSESPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

// ── Helpers (mirror from LowerNarvalToStandardPass) ──────────────────────────

static func::FuncOp ensure_decl(ModuleOp module, OpBuilder& builder,
                                  llvm::StringRef name, FunctionType ft) {
    if (auto f = module.lookupSymbol<func::FuncOp>(name)) return f;
    OpBuilder::InsertionGuard g(builder);
    builder.setInsertionPointToStart(module.getBody());
    auto f = func::FuncOp::create(builder, builder.getUnknownLoc(), name, ft);
    f.setPrivate();
    return f;
}

static Value str_global(ModuleOp module, OpBuilder& builder, Location loc,
                         llvm::StringRef content) {
    auto gname = ("__narval_cls_" + content).str();
    auto* ctx  = builder.getContext();
    auto ptr   = LLVM::LLVMPointerType::get(ctx);
    if (!module.lookupSymbol(gname)) {
        OpBuilder::InsertionGuard g(builder);
        builder.setInsertionPointToStart(module.getBody());
        std::string with_null(content.str());
        with_null.push_back('\0');
        auto arr = LLVM::LLVMArrayType::get(builder.getIntegerType(8),
                                             with_null.size());
        LLVM::GlobalOp::create(builder, loc, arr, true,
                               LLVM::Linkage::Internal, gname,
                               builder.getStringAttr(
                                   llvm::StringRef(with_null.data(),
                                                   with_null.size())));
    }
    return LLVM::AddressOfOp::create(builder, loc, ptr, gname);
}

// ── narval.new → nv_alloc_object + __ctor_ClassName ──────────────────────────

struct LowerNewOp : public OpConversionPattern<NewOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(NewOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        auto cname = op.getClassName();

        // 1. nv_alloc_object(class_name_ptr) → this_ptr
        ensure_decl(mod, r, "nv_alloc_object",
                    FunctionType::get(ctx, {ptr}, {ptr}));
        auto cname_ptr = str_global(mod, r, loc, cname);
        auto this_ptr  = func::CallOp::create(r, loc, TypeRange{ptr},
                                               "nv_alloc_object",
                                               ValueRange{cname_ptr})
                             .getResult(0);

        // 2. __ctor_ClassName(this, ctor_args...) — void return
        auto ctor_name = ("__ctor_" + cname).str();
        SmallVector<Type> ctor_params(1 + adaptor.getCtorArgs().size(), ptr);
        ensure_decl(mod, r, ctor_name,
                    FunctionType::get(ctx, ctor_params, {}));
        SmallVector<Value> ctor_args;
        ctor_args.push_back(this_ptr);
        for (auto a : adaptor.getCtorArgs()) ctor_args.push_back(a);
        func::CallOp::create(r, loc, TypeRange{}, ctor_name, ctor_args);

        r.replaceOp(op, this_ptr);
        return success();
    }
};

// ── narval.get_field → nv_get_field ──────────────────────────────────────────

struct LowerGetFieldOp : public OpConversionPattern<GetFieldOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(GetFieldOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        ensure_decl(mod, r, "nv_get_field",
                    FunctionType::get(ctx, {ptr, ptr}, {ptr}));
        auto key = str_global(mod, r, loc, op.getFieldName());
        r.replaceOp(op,
            func::CallOp::create(r, loc, TypeRange{ptr}, "nv_get_field",
                                  ValueRange{adaptor.getObject(), key})
                .getResult(0));
        return success();
    }
};

// ── narval.set_field → nv_set_field ──────────────────────────────────────────

struct LowerSetFieldOp : public OpConversionPattern<SetFieldOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(SetFieldOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        ensure_decl(mod, r, "nv_set_field",
                    FunctionType::get(ctx, {ptr, ptr, ptr}, {}));
        auto key = str_global(mod, r, loc, op.getFieldName());
        func::CallOp::create(r, loc, TypeRange{}, "nv_set_field",
                              ValueRange{adaptor.getObject(), key,
                                        adaptor.getValue()});
        r.eraseOp(op);
        return success();
    }
};

// ── narval.call_method → __method_ClassName_name ─────────────────────────────

struct LowerCallMethodOp : public OpConversionPattern<CallMethodOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(CallMethodOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc   = op.getLoc();
        auto* ctx  = r.getContext();
        auto ptr   = LLVM::LLVMPointerType::get(ctx);
        auto mod   = op->getParentOfType<ModuleOp>();
        auto mname = ("__method_" + op.getClassName() + "_"
                      + op.getMethodName()).str();
        SmallVector<Type> params(1 + adaptor.getArgs().size(), ptr);
        ensure_decl(mod, r, mname, FunctionType::get(ctx, params, {ptr}));
        SmallVector<Value> args;
        args.push_back(adaptor.getReceiver());
        for (auto a : adaptor.getArgs()) args.push_back(a);
        r.replaceOp(op,
            func::CallOp::create(r, loc, TypeRange{ptr}, mname, args)
                .getResult(0));
        return success();
    }
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct LowerNarvalClassesPassImpl
    : public ::impl::LowerNarvalClassesPassBase<LowerNarvalClassesPassImpl> {

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
        target.addLegalDialect<func::FuncDialect, LLVM::LLVMDialect>();
        target.addLegalOp<ModuleOp>();
        target.addIllegalOp<NewOp, GetFieldOp, SetFieldOp, CallMethodOp>();
        target.markUnknownOpDynamicallyLegal([](Operation*) { return true; });

        RewritePatternSet patterns(ctx);
        patterns.add<LowerNewOp, LowerGetFieldOp, LowerSetFieldOp,
                     LowerCallMethodOp>(tc, ctx);

        if (failed(applyPartialConversion(module, target, std::move(patterns)))) {
            module.emitError("lower-narval-classes: conversion failed");
            signalPassFailure();
        }
    }
};

} // namespace

std::unique_ptr<mlir::Pass> createLowerNarvalClassesPass() {
    return std::make_unique<LowerNarvalClassesPassImpl>();
}

} // namespace nv
