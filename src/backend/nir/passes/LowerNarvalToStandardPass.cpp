#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"

#include "mlir/Conversion/LLVMCommon/ConversionTarget.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/Passes.h"

#define GEN_PASS_DEF_LOWERNARVALTOSTANDARDPASS
#include "NarvalPasses.h.inc"

using namespace mlir;
using namespace mlir::narval;

namespace nv {
namespace {

class NarvalTypeConverter : public TypeConverter {
public:
    explicit NarvalTypeConverter(MLIRContext* ctx) {
        auto ptr_ty = LLVM::LLVMPointerType::get(ctx);
        addConversion([ptr_ty](narval::ValueType)  -> Type { return ptr_ty; });
        addConversion([ptr_ty](narval::RefType)    -> Type { return ptr_ty; });
        addConversion([ptr_ty](narval::MutRefType) -> Type { return ptr_ty; });
        addConversion([](Type t) -> std::optional<Type> {
            if (mlir::isa<narval::ValueType, narval::RefType, narval::MutRefType>(t))
                return std::nullopt;
            return t;
        });
    }
};

static func::FuncOp ensure_decl(ModuleOp module, OpBuilder& builder,
                                  llvm::StringRef name, FunctionType ft) {
    if (auto f = module.lookupSymbol<func::FuncOp>(name)) return f;
    OpBuilder::InsertionGuard g(builder);
    builder.setInsertionPointToStart(module.getBody());
    auto f = func::FuncOp::create(builder, builder.getUnknownLoc(), name, ft);
    f.setPrivate();
    return f;
}

static Value str_ptr(ModuleOp module, OpBuilder& builder, Location loc,
                      llvm::StringRef content) {
    auto gname = ("__narval_str_" + content).str();
    auto* ctx  = builder.getContext();
    auto ptr   = LLVM::LLVMPointerType::get(ctx);
    if (!module.lookupSymbol(gname)) {
        OpBuilder::InsertionGuard g(builder);
        builder.setInsertionPointToStart(module.getBody());
        // Build the null-terminated string via std::string so the null byte is
        // included in the StringRef length (StringRef(const char*) uses strlen
        // and would strip a bare "\0" appended via Twine).
        std::string with_null(content.str());
        with_null.push_back('\0');
        auto arr = LLVM::LLVMArrayType::get(builder.getIntegerType(8),
                                             with_null.size());
        LLVM::GlobalOp::create(builder, loc, arr, true,
                               LLVM::Linkage::Internal, gname,
                               builder.getStringAttr(
                                   llvm::StringRef(with_null.data(), with_null.size())));
    }
    return LLVM::AddressOfOp::create(builder, loc, ptr, gname);
}

struct LowerAllocOp : public OpConversionPattern<AllocOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(AllocOp op, OpAdaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc(); auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        auto mod = op->getParentOfType<ModuleOp>();
        ensure_decl(mod, r, op.getRuntimeCtor(), FunctionType::get(ctx,{},{ptr}));
        r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                              op.getRuntimeCtor(), ValueRange{})
                           .getResult(0));
        return success();
    }
};

struct LowerDropOp : public OpConversionPattern<DropOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(DropOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc(); auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        ensure_decl(op->getParentOfType<ModuleOp>(), r, "nv_free",
                    FunctionType::get(ctx,{ptr},{}));
        func::CallOp::create(r, loc, TypeRange{}, "nv_free",
                              ValueRange{a.getValue()});
        r.eraseOp(op); return success();
    }
};

struct LowerMoveOp : public OpConversionPattern<MoveOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(MoveOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOp(op, a.getSource()); return success();
    }
};

struct LowerBorrowOp : public OpConversionPattern<BorrowOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(BorrowOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOp(op, a.getSource()); return success();
    }
};

struct LowerBorrowMutOp : public OpConversionPattern<BorrowMutOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(BorrowMutOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOp(op, a.getSource()); return success();
    }
};

struct LowerCallOp : public OpConversionPattern<CallOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(CallOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        SmallVector<Type> rt;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), rt)))
            return failure();
        r.replaceOpWithNewOp<func::CallOp>(op, rt, op.getCallee(), a.getOperands());
        return success();
    }
};

struct LowerCallRuntimeOp : public OpConversionPattern<CallRuntimeOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(CallRuntimeOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        SmallVector<Type> rt;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), rt)))
            return failure();

        // Collect the converted argument types from the adapted operands.
        SmallVector<Type> arg_types;
        for (auto v : a.getOperands()) arg_types.push_back(v.getType());

        // Re-declare the function with the fully-converted signature so the
        // resulting func::CallOp doesn't need a !narval.value → !llvm.ptr
        // materialization at the call site.
        auto mod = op->getParentOfType<ModuleOp>();
        auto ft  = FunctionType::get(r.getContext(), arg_types, rt);
        ensure_decl(mod, r, op.getCallee().str(), ft);

        r.replaceOpWithNewOp<func::CallOp>(op, rt, op.getCallee(), a.getOperands());
        return success();
    }
};

struct LowerReturnOp : public OpConversionPattern<ReturnOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(ReturnOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        r.replaceOpWithNewOp<func::ReturnOp>(op, a.getOperands());
        return success();
    }
};

struct LowerConstantOp : public OpConversionPattern<ConstantOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(ConstantOp op, OpAdaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        auto mod = op->getParentOfType<ModuleOp>();

        if (auto ia = mlir::dyn_cast<IntegerAttr>(op.getValue())) {
            auto i64 = r.getI64Type();
            ensure_decl(mod, r, "nv_box_int", FunctionType::get(ctx, {i64}, {ptr}));
            auto raw = arith::ConstantOp::create(r, loc,
                IntegerAttr::get(i64, ia.getInt()));
            r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                                  "nv_box_int",
                                                  ValueRange{raw.getResult()})
                               .getResult(0));
            return success();
        }
        if (auto fa = mlir::dyn_cast<FloatAttr>(op.getValue())) {
            auto f64 = r.getF64Type();
            ensure_decl(mod, r, "nv_box_float", FunctionType::get(ctx, {f64}, {ptr}));
            auto raw = arith::ConstantOp::create(r, loc,
                FloatAttr::get(f64, fa.getValueAsDouble()));
            r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                                  "nv_box_float",
                                                  ValueRange{raw.getResult()})
                               .getResult(0));
            return success();
        }
        if (auto sa = mlir::dyn_cast<StringAttr>(op.getValue())) {
            ensure_decl(mod, r, "nv_box_str", FunctionType::get(ctx, {ptr}, {ptr}));
            auto s = str_ptr(mod, r, loc, sa.getValue());
            r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                                  "nv_box_str", ValueRange{s})
                               .getResult(0));
            return success();
        }
        return failure();
    }
};

struct LowerComptimeConstOp : public OpConversionPattern<ComptimeConstOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(ComptimeConstOp op, OpAdaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        auto mod = op->getParentOfType<ModuleOp>();

        if (auto ia = mlir::dyn_cast<IntegerAttr>(op.getValue())) {
            auto i64 = r.getI64Type();
            ensure_decl(mod, r, "nv_box_int", FunctionType::get(ctx, {i64}, {ptr}));
            auto raw = arith::ConstantOp::create(r, loc,
                IntegerAttr::get(i64, ia.getInt()));
            r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                                  "nv_box_int",
                                                  ValueRange{raw.getResult()})
                               .getResult(0));
            return success();
        }
        if (auto fa = mlir::dyn_cast<FloatAttr>(op.getValue())) {
            auto f64 = r.getF64Type();
            ensure_decl(mod, r, "nv_box_float", FunctionType::get(ctx, {f64}, {ptr}));
            auto raw = arith::ConstantOp::create(r, loc,
                FloatAttr::get(f64, fa.getValueAsDouble()));
            r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                                  "nv_box_float",
                                                  ValueRange{raw.getResult()})
                               .getResult(0));
            return success();
        }
        return failure();
    }
};

//===----------------------------------------------------------------------===//
// The pass struct
//===----------------------------------------------------------------------===//

struct LowerNarvalToStandardPassImpl
    : public ::impl::LowerNarvalToStandardPassBase<LowerNarvalToStandardPassImpl> {

    void runOnOperation() override {
        ModuleOp module = getOperation();
        MLIRContext* ctx = &getContext();
        NarvalTypeConverter tc(ctx);
        ConversionTarget target(*ctx);

        target.addLegalDialect<func::FuncDialect, arith::ArithDialect,
                               scf::SCFDialect, memref::MemRefDialect,
                               LLVM::LLVMDialect>();
        target.addLegalOp<ModuleOp>();
        target.addIllegalOp<AllocOp, DropOp, MoveOp, BorrowOp, BorrowMutOp,
                            CallOp, CallRuntimeOp, ReturnOp,
                            ConstantOp, ComptimeConstOp>();
        // Class ops handled by LowerNarvalClassesPass (runs before this pass).
        target.addLegalOp<NewOp, GetFieldOp, SetFieldOp, CallMethodOp>();
        // Tensor and GPU stay for later passes.
        target.addLegalOp<TensorMatmulOp, TensorAddOp, TensorMulOp,
                          TensorTransposeOp, TensorFillOp,
                          GPULaunchOp, GPUThreadIdOp, GPUBlockIdOp>();
        // CF ops are already lowered by LowerNarvalControlFlowPass.
        target.addLegalOp<IfOp, ForRangeOp, WhileOp, YieldOp>();

        RewritePatternSet patterns(ctx);
        patterns.add<LowerAllocOp, LowerDropOp, LowerMoveOp, LowerBorrowOp,
                     LowerBorrowMutOp, LowerCallOp, LowerCallRuntimeOp,
                     LowerReturnOp, LowerConstantOp, LowerComptimeConstOp>(
            tc, ctx);

        if (failed(applyPartialConversion(module, target, std::move(patterns)))) {
            module.emitError("lower-narval-to-std: conversion failed");
            signalPassFailure();
        }
    }
};

} // namespace

//===----------------------------------------------------------------------===//
// Public creation functions for all passes in this file
//===----------------------------------------------------------------------===//

std::unique_ptr<mlir::Pass> createLowerNarvalToStandardPass() {
    return std::make_unique<LowerNarvalToStandardPassImpl>();
}

} // namespace nv
