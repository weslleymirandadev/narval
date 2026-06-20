#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalPasses.h"
#include "backend/nir/NarvalTypes.h"

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
        auto callee = op.getRuntimeCtor();
        auto ft = FunctionType::get(ctx,{}, {ptr});
        if (auto f = mod.lookupSymbol<func::FuncOp>(callee)) {
            if (f.getFunctionType() != ft) f.erase();
        }
        ensure_decl(mod, r, callee, ft);
        r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                              callee, ValueRange{})
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
        auto mod = op->getParentOfType<ModuleOp>();
        auto ft = FunctionType::get(ctx,{ptr},{});
        if (auto f = mod.lookupSymbol<func::FuncOp>("nv_free")) {
            if (f.getFunctionType() != ft) f.erase();
        }
        ensure_decl(mod, r, "nv_free", ft);
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

struct LowerNarvalCallOp : public OpConversionPattern<CallOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(CallOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        SmallVector<Type> rt;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), rt)))
            return failure();

        // Update the function signature to the converted types.
        auto mod = op->getParentOfType<ModuleOp>();
        if (auto fn = mod.lookupSymbol<func::FuncOp>(op.getCallee())) {
            SmallVector<Type> param_types;
            for (auto v : a.getOperands()) param_types.push_back(v.getType());
            auto new_ft = FunctionType::get(r.getContext(), param_types, rt);
            if (fn.getFunctionType() != new_ft) {
                fn.erase();
                ensure_decl(mod, r, op.getCallee().str(), new_ft);
            }
        }

        r.replaceOpWithNewOp<func::CallOp>(op, rt, op.getCallee(), a.getOperands());
        return success();
    }
};

struct LowerCallOp : public OpConversionPattern<func::CallOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(func::CallOp op, OpAdaptor a,
                                  ConversionPatternRewriter& r) const override {
        // Only handle func.call ops that have narval types that need conversion.
        bool needs_conversion = false;
        for (auto t : op.getOperandTypes())
            if (mlir::isa<narval::ValueType, narval::RefType, narval::MutRefType>(t))
                needs_conversion = true;
        for (auto t : op.getResultTypes())
            if (mlir::isa<narval::ValueType, narval::RefType, narval::MutRefType>(t))
                needs_conversion = true;
        if (!needs_conversion)
            return failure();

        SmallVector<Type> rt;
        if (failed(typeConverter->convertTypes(op.getResultTypes(), rt)))
            return failure();

        // Update the function signature to the converted types so the
        // resulting func::CallOp doesn't need a materialization cast.
        auto mod = op->getParentOfType<ModuleOp>();
        if (auto fn = mod.lookupSymbol<func::FuncOp>(op.getCallee())) {
            SmallVector<Type> param_types;
            for (auto v : a.getOperands()) param_types.push_back(v.getType());
            auto new_ft = FunctionType::get(r.getContext(), param_types, rt);
            if (fn.getFunctionType() != new_ft) {
                fn.erase();
                ensure_decl(mod, r, op.getCallee().str(), new_ft);
            }
        }

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
        // Remove any prior declaration with incompatible signature so
        // ensure_decl creates the correct one.
        if (auto existing = mod.lookupSymbol<func::FuncOp>(op.getCallee())) {
            if (existing.getFunctionType() != ft)
                existing.erase();
        }
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

//===----------------------------------------------------------------------===//
// narval.constant → nv_box_int / nv_box_float / nv_box_str
//===----------------------------------------------------------------------===//

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
            auto ft  = FunctionType::get(ctx, {i64}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_box_int")) {
                if (f.getFunctionType() != ft) f.erase();
            }
            ensure_decl(mod, r, "nv_box_int", ft);
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
            auto ft  = FunctionType::get(ctx, {f64}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_box_float")) {
                if (f.getFunctionType() != ft) f.erase();
            }
            ensure_decl(mod, r, "nv_box_float", ft);
            auto raw = arith::ConstantOp::create(r, loc,
                FloatAttr::get(f64, fa.getValueAsDouble()));
            r.replaceOp(op, func::CallOp::create(r, loc, TypeRange{ptr},
                                                  "nv_box_float",
                                                  ValueRange{raw.getResult()})
                               .getResult(0));
            return success();
        }
        if (auto sa = mlir::dyn_cast<StringAttr>(op.getValue())) {
            auto ft  = FunctionType::get(ctx, {ptr}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_box_str")) {
                if (f.getFunctionType() != ft) f.erase();
            }
            ensure_decl(mod, r, "nv_box_str", ft);
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
            auto ft  = FunctionType::get(ctx, {i64}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_box_int")) {
                if (f.getFunctionType() != ft) f.erase();
            }
            ensure_decl(mod, r, "nv_box_int", ft);
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
            auto ft  = FunctionType::get(ctx, {f64}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_box_float")) {
                if (f.getFunctionType() != ft) f.erase();
            }
            ensure_decl(mod, r, "nv_box_float", ft);
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
// narval.new → nv_alloc_object + __ctor_ClassName
//===----------------------------------------------------------------------===//

struct LowerNewOp : public OpConversionPattern<NewOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(NewOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        auto cname = op.getClassName();

        {
            auto alloc_ft = FunctionType::get(ctx, {ptr}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_alloc_object")) {
                if (f.getFunctionType() != alloc_ft) f.erase();
            }
            ensure_decl(mod, r, "nv_alloc_object", alloc_ft);
        }
        auto cname_ptr = str_ptr(mod, r, loc, cname);
        auto this_ptr  = func::CallOp::create(r, loc, TypeRange{ptr},
                                               "nv_alloc_object",
                                               ValueRange{cname_ptr})
                             .getResult(0);

        auto ctor_name = ("__ctor_" + cname).str();
        SmallVector<Type> ctor_params(1 + adaptor.getCtorArgs().size(), ptr);
        {
            auto ctor_ft = FunctionType::get(ctx, ctor_params, {});
            if (auto f = mod.lookupSymbol<func::FuncOp>(ctor_name)) {
                if (f.getFunctionType() != ctor_ft) f.erase();
            }
            ensure_decl(mod, r, ctor_name, ctor_ft);
        }
        SmallVector<Value> ctor_args;
        ctor_args.push_back(this_ptr);
        for (auto a : adaptor.getCtorArgs()) ctor_args.push_back(a);
        func::CallOp::create(r, loc, TypeRange{}, ctor_name, ctor_args);

        r.replaceOp(op, this_ptr);
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.get_field → nv_get_field
//===----------------------------------------------------------------------===//

struct LowerGetFieldOp : public OpConversionPattern<GetFieldOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(GetFieldOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        {
            auto gf_ft = FunctionType::get(ctx, {ptr, ptr}, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_get_field")) {
                if (f.getFunctionType() != gf_ft) f.erase();
            }
            ensure_decl(mod, r, "nv_get_field", gf_ft);
        }
        auto key = str_ptr(mod, r, loc, op.getFieldName());
        r.replaceOp(op,
            func::CallOp::create(r, loc, TypeRange{ptr}, "nv_get_field",
                                  ValueRange{adaptor.getObject(), key})
                .getResult(0));
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.set_field → nv_set_field
//===----------------------------------------------------------------------===//

struct LowerSetFieldOp : public OpConversionPattern<SetFieldOp> {
    using OpConversionPattern::OpConversionPattern;
    LogicalResult matchAndRewrite(SetFieldOp op, OpAdaptor adaptor,
                                  ConversionPatternRewriter& r) const override {
        auto loc  = op.getLoc();
        auto* ctx = r.getContext();
        auto ptr  = LLVM::LLVMPointerType::get(ctx);
        auto mod  = op->getParentOfType<ModuleOp>();
        {
            auto sf_ft = FunctionType::get(ctx, {ptr, ptr, ptr}, {});
            if (auto f = mod.lookupSymbol<func::FuncOp>("nv_set_field")) {
                if (f.getFunctionType() != sf_ft) f.erase();
            }
            ensure_decl(mod, r, "nv_set_field", sf_ft);
        }
        auto key = str_ptr(mod, r, loc, op.getFieldName());
        func::CallOp::create(r, loc, TypeRange{}, "nv_set_field",
                              ValueRange{adaptor.getObject(), key,
                                        adaptor.getValue()});
        r.eraseOp(op);
        return success();
    }
};

//===----------------------------------------------------------------------===//
// narval.call_method → __method_ClassName_name
//===----------------------------------------------------------------------===//

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
        {
            auto cm_ft = FunctionType::get(ctx, params, {ptr});
            if (auto f = mod.lookupSymbol<func::FuncOp>(mname)) {
                if (f.getFunctionType() != cm_ft) f.erase();
            }
            ensure_decl(mod, r, mname, cm_ft);
        }
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
        // Mark func.call as dynamically legal — illegal when it has narval types.
        target.addDynamicallyLegalOp<func::CallOp>([](func::CallOp op) {
            for (auto t : op.getOperandTypes())
                if (mlir::isa<narval::ValueType, narval::RefType, narval::MutRefType>(t))
                    return false;
            for (auto t : op.getResultTypes())
                if (mlir::isa<narval::ValueType, narval::RefType, narval::MutRefType>(t))
                    return false;
            return true;
        });
        target.addIllegalOp<AllocOp, DropOp, MoveOp, BorrowOp, BorrowMutOp,
                            CallOp, CallRuntimeOp, ReturnOp,
                            ConstantOp, ComptimeConstOp,
                            NewOp, GetFieldOp, SetFieldOp, CallMethodOp>();
        // Tensor and GPU stay for later passes.
        target.addLegalOp<TensorMatmulOp, TensorAddOp, TensorMulOp,
                          TensorTransposeOp, TensorFillOp,
                          GPULaunchOp, GPUThreadIdOp, GPUBlockIdOp>();
        // CF ops are already lowered by LowerNarvalControlFlowPass.
        target.addLegalOp<IfOp, ForRangeOp, WhileOp, YieldOp>();

        RewritePatternSet patterns(ctx);
        patterns.add<LowerAllocOp, LowerDropOp, LowerMoveOp, LowerBorrowOp,
                     LowerBorrowMutOp, LowerNarvalCallOp, LowerCallOp, LowerCallRuntimeOp,
                     LowerReturnOp, LowerConstantOp, LowerComptimeConstOp,
                     LowerNewOp, LowerGetFieldOp, LowerSetFieldOp,
                     LowerCallMethodOp>(tc, ctx);

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

//===----------------------------------------------------------------------===//
// FixSCFIfTypesPass — Fix scf.if result types that still have !narval.value
// Also updates scf.yield operands to match the new result types.
//===----------------------------------------------------------------------===//

struct FixSCFIfTypesPassImpl
    : public mlir::PassWrapper<FixSCFIfTypesPassImpl,
                                mlir::OperationPass<mlir::ModuleOp>> {
    MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FixSCFIfTypesPassImpl)

    StringRef getName() const override { return "fix-scf-if-types"; }

    void runOnOperation() override {
        mlir::ModuleOp module = getOperation();
        llvm::SmallVector<mlir::scf::IfOp> if_ops;
        module.walk([&](mlir::scf::IfOp op) {
            bool has_narval = false;
            for (auto t : op.getResultTypes())
                if (mlir::isa<narval::ValueType, narval::RefType,
                              narval::MutRefType>(t))
                    has_narval = true;
            if (has_narval)
                if_ops.push_back(op);
        });

        for (auto op : if_ops) {
            mlir::OpBuilder b(op);
            auto loc = op.getLoc();

            auto ptr_ty = LLVM::LLVMPointerType::get(b.getContext());
            llvm::SmallVector<mlir::Type> res_types;
            llvm::SmallVector<unsigned> converted_indices;
            for (unsigned i = 0; i < op.getNumResults(); ++i) {
                auto t = op.getResultTypes()[i];
                if (mlir::isa<narval::ValueType, narval::RefType,
                              narval::MutRefType>(t)) {
                    res_types.push_back(ptr_ty);
                    converted_indices.push_back(i);
                } else {
                    res_types.push_back(t);
                }
            }

            bool has_else = !op.getElseRegion().empty() &&
                            !op.getElseRegion().front().empty();

            auto new_if = mlir::scf::IfOp::create(b, loc, res_types,
                                                   op.getCondition(),
                                                   has_else);

            // Fix then region: update yield operands and move region
            {
                auto* old_then = &op.getThenRegion().front();
                auto* new_then_blk = &new_if.getThenRegion().front();
                // Fix yield operands in then region: for converted indices,
                // strip unrealized_conversion_cast if present
                auto& then_ops = old_then->getOperations();
                if (!then_ops.empty()) {
                    auto* term = &then_ops.back();
                    if (auto yield = mlir::dyn_cast<mlir::scf::YieldOp>(term)) {
                        bool changed = false;
                        llvm::SmallVector<mlir::Value> new_operands;
                        for (unsigned j = 0; j < yield.getNumOperands(); ++j) {
                            auto val = yield.getOperand(j);
                            // If this operand is at a converted index and is
                            // produced by an unrealized_conversion_cast, strip it
                            if (std::find(converted_indices.begin(),
                                          converted_indices.end(), j)
                                    != converted_indices.end()) {
                                if (auto def_op = val.getDefiningOp()) {
                                    if (mlir::isa<mlir::UnrealizedConversionCastOp>(def_op)) {
                                        new_operands.push_back(def_op->getOperand(0));
                                        def_op->erase();
                                        changed = true;
                                        continue;
                                    }
                                }
                            }
                            new_operands.push_back(val);
                        }
                        if (changed) {
                            yield->setOperands(new_operands);
                        }
                    }
                }
                // Move the fixed block into the new if
                old_then->moveBefore(new_then_blk);
                new_then_blk->erase();
            }

            // Fix else region: same yield operand fix
            if (has_else) {
                auto* old_else = &op.getElseRegion().front();
                auto* new_else_blk = &new_if.getElseRegion().front();
                auto& else_ops = old_else->getOperations();
                if (!else_ops.empty()) {
                    auto* term = &else_ops.back();
                    if (auto yield = mlir::dyn_cast<mlir::scf::YieldOp>(term)) {
                        bool changed = false;
                        llvm::SmallVector<mlir::Value> new_operands;
                        for (unsigned j = 0; j < yield.getNumOperands(); ++j) {
                            auto val = yield.getOperand(j);
                            if (std::find(converted_indices.begin(),
                                          converted_indices.end(), j)
                                    != converted_indices.end()) {
                                if (auto def_op = val.getDefiningOp()) {
                                    if (mlir::isa<mlir::UnrealizedConversionCastOp>(def_op)) {
                                        new_operands.push_back(def_op->getOperand(0));
                                        def_op->erase();
                                        changed = true;
                                        continue;
                                    }
                                }
                            }
                            new_operands.push_back(val);
                        }
                        if (changed) {
                            yield->setOperands(new_operands);
                        }
                    }
                }
                old_else->moveBefore(new_else_blk);
                new_else_blk->erase();
            }

            op.replaceAllUsesWith(new_if.getResults());
            op.erase();
        }
    }
};

std::unique_ptr<mlir::Pass> createFixSCFIfTypesPass() {
    return std::make_unique<FixSCFIfTypesPassImpl>();
}

} // namespace nv
