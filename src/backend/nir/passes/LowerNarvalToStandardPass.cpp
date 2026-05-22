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
        auto arr = LLVM::LLVMArrayType::get(builder.getIntegerType(8),
                                             content.size() + 1);
        LLVM::GlobalOp::create(builder, loc, arr, true,
                               LLVM::Linkage::Internal, gname,
                               builder.getStringAttr((content + "\0").str()));
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

struct LowerGetFieldOp : public OpConversionPattern<GetFieldOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(GetFieldOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc(); auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        auto mod = op->getParentOfType<ModuleOp>();
        ensure_decl(mod, r, "nv_object_get_field",
                    FunctionType::get(ctx,{ptr,ptr},{ptr}));
        auto key = str_ptr(mod, r, loc, op.getFieldName());
        r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                              "nv_object_get_field",
                                              ValueRange{a.getObject(), key})
                           .getResult(0));
        return success();
    }
};

struct LowerSetFieldOp : public OpConversionPattern<SetFieldOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(SetFieldOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc(); auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        auto mod = op->getParentOfType<ModuleOp>();
        ensure_decl(mod, r, "nv_object_set_field",
                    FunctionType::get(ctx,{ptr,ptr,ptr},{}));
        auto key = str_ptr(mod, r, loc, op.getFieldName());
        func::CallOp::create(r, loc, TypeRange{}, "nv_object_set_field",
                              ValueRange{a.getObject(), key, a.getValue()});
        r.eraseOp(op); return success();
    }
};

struct LowerCallMethodOp : public OpConversionPattern<CallMethodOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(CallMethodOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        auto loc = op.getLoc(); auto* ctx = r.getContext();
        auto ptr = LLVM::LLVMPointerType::get(ctx);
        auto mod = op->getParentOfType<ModuleOp>();
        auto name = ("__method_" + op.getClassName() + "_"
                     + op.getMethodName()).str();
        SmallVector<Type> params(a.getOperands().size(), ptr);
        ensure_decl(mod, r, name, FunctionType::get(ctx, params, {ptr}));
        SmallVector<Value> args;
        args.push_back(a.getReceiver());
        for (auto v : a.getArgs()) args.push_back(v);
        r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr}, name, args)
                           .getResult(0));
        return success();
    }
};

struct LowerConstantOp : public OpConversionPattern<ConstantOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(ConstantOp op, OpAdaptor,
                                  ConversionPatternRewriter& r) const override {
        if (mlir::isa<IntegerAttr, FloatAttr>(op.getValue())) {
            r.replaceOpWithNewOp<arith::ConstantOp>(
                op, mlir::cast<TypedAttr>(op.getValue()));
            return success();
        }
        return failure();
    }
};

struct LowerComptimeConstOp : public OpConversionPattern<ComptimeConstOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(ComptimeConstOp op, OpAdaptor,
                                  ConversionPatternRewriter& r) const override {
        if (mlir::isa<IntegerAttr, FloatAttr>(op.getValue())) {
            r.replaceOpWithNewOp<arith::ConstantOp>(
                op, mlir::cast<TypedAttr>(op.getValue()));
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
                            GetFieldOp, SetFieldOp, CallMethodOp,
                            ConstantOp, ComptimeConstOp>();
        // Tensor and GPU stay for later passes.
        target.addLegalOp<TensorMatmulOp, TensorAddOp, TensorMulOp,
                          TensorTransposeOp, TensorFillOp,
                          GPULaunchOp, GPUThreadIdOp, GPUBlockIdOp>();
        // Phase 2 CF ops are already lowered by LowerNarvalControlFlowPass.
        target.addLegalOp<IfOp, ForRangeOp, WhileOp, YieldOp>();

        RewritePatternSet patterns(ctx);
        patterns.add<LowerAllocOp, LowerDropOp, LowerMoveOp, LowerBorrowOp,
                     LowerBorrowMutOp, LowerCallOp, LowerCallRuntimeOp,
                     LowerReturnOp, LowerGetFieldOp, LowerSetFieldOp,
                     LowerCallMethodOp, LowerConstantOp, LowerComptimeConstOp>(
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

// Note: createLowerNarvalTensorPass() is in LowerTensorToLinalgPass.cpp (Phase 4)
//       createLowerNarvalGPUPass()    is in LowerGPUKernelsPass.cpp    (Phase 8)
//       createNarvalOwnershipPass()   is in LowerOwnershipPass.cpp     (Phase 3)
//
// registerNarvalPasses() is generated by GEN_PASS_REGISTRATION in NarvalPasses.h.inc.

} // namespace nv
