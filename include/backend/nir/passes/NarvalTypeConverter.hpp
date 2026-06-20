#pragma once

#include "backend/nir/NarvalOps.h"
#include "backend/nir/NarvalTypes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir::narval {

// ── Type converter: !narval.value → !llvm.ptr, !narval.ref → !llvm.ptr ─────
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

// ── Ensure a func::FuncOp declaration exists at module top ────────────────
inline func::FuncOp ensure_decl(ModuleOp module, OpBuilder& builder,
                                 llvm::StringRef name, FunctionType ft) {
    if (auto f = module.lookupSymbol<func::FuncOp>(name)) return f;
    OpBuilder::InsertionGuard g(builder);
    builder.setInsertionPointToStart(module.getBody());
    auto f = func::FuncOp::create(builder, builder.getUnknownLoc(), name, ft);
    f.setPrivate();
    return f;
}

// ── Emit a null-terminated LLVM global string constant ────────────────────
inline Value str_ptr(ModuleOp module, OpBuilder& builder, Location loc,
                      llvm::StringRef content) {
    auto gname = ("__narval_str_" + content).str();
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

// ── Helper: erase a function declaration if its signature changed ─────────
inline void ensure_func_sig(ModuleOp mod, OpBuilder& builder, llvm::StringRef name, FunctionType ft) {
    if (auto f = mod.lookupSymbol<func::FuncOp>(name)) {
        if (f.getFunctionType() != ft) f.erase();
    }
    ensure_decl(mod, builder, name, ft);
}

} // namespace mlir::narval
