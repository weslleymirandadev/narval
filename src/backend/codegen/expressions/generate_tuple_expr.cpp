#include "frontend/ast/expressions/tuple_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/DerivedTypes.h>

void TupleExprNode::codegen(rph::IRGenerationContext& ctx) {
    auto& c = ctx.get_context();
    auto& b = ctx.get_builder();
    auto& m = ctx.get_module();
    unsigned N = static_cast<unsigned>(elements.size());

    // Tipos úteis
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    auto* I32 = llvm::Type::getInt32Ty(c);
    auto* F64 = llvm::Type::getDoubleTy(c);
    auto* I8P = rph::ir_utils::get_i8_ptr(ctx);

    // Declarações do runtime
    auto ensure_create_tuple = [&]() {
        return m.getOrInsertFunction(
            "create_tuple",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I32}, false)
        );
    };
    auto ensure_tuple_set = [&]() {
        return m.getOrInsertFunction(
            "tuple_set_impl",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I32, ValuePtr}, false)
        );
    };
    auto ensure_create_bool = [&]() {
        return m.getOrInsertFunction(
            "create_bool",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I32}, false)
        );
    };
    auto ensure_create_int = [&]() {
        return m.getOrInsertFunction(
            "create_int",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I32}, false)
        );
    };
    auto ensure_create_float = [&]() {
        return m.getOrInsertFunction(
            "create_float",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, F64}, false)
        );
    };
    auto ensure_create_str = [&]() {
        return m.getOrInsertFunction(
            "create_str",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I8P}, false)
        );
    };

    // Alocar Value para o tuplo e inicializar via runtime
    auto* tupAlloca = ctx.create_alloca(ValueTy, "tuple.val");
    b.CreateCall(llvm::cast<llvm::Function>(ensure_create_tuple().getCallee()), {tupAlloca, llvm::ConstantInt::get(I32, N)});

    // Função para box de um elemento em Value*
    auto box_elem = [&](llvm::Value* any) -> llvm::Value* {
        auto* tmp = ctx.create_alloca(ValueTy, "tuple.elem");
        if (!any) {
            b.CreateStore(llvm::UndefValue::get(ValueTy), tmp);
            return tmp;
        }
        auto* T = any->getType();
        if (T == ValueTy) {
            b.CreateStore(any, tmp);
        } else if (T->isIntegerTy(1)) {
            auto decl = ensure_create_bool();
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, b.CreateZExt(any, I32)});
        } else if (T->isIntegerTy()) {
            llvm::Value* iv = T->isIntegerTy(32) ? any : b.CreateSExtOrTrunc(any, I32);
            auto decl = ensure_create_int();
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, iv});
        } else if (T->isFloatingPointTy()) {
            llvm::Value* fp = (T == F64) ? any : b.CreateFPExt(any, F64);
            auto decl = ensure_create_float();
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, fp});
        } else if (T->isPointerTy()) {
            // Trate ponteiros como string (bitcast para i8*)
            llvm::Value* s = (T == I8P) ? any : b.CreateBitCast(any, I8P);
            auto decl = ensure_create_str();
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, s});
        } else {
            // Fallback: value indefinido para tipos não tratados aqui
            b.CreateStore(llvm::UndefValue::get(ValueTy), tmp);
        }
        return tmp;
    };

    // Gerar e inserir cada elemento pelo runtime
    for (unsigned i = 0; i < N; ++i) {
        llvm::Value* ev = nullptr;
        if (elements[i]) { elements[i]->codegen(ctx); if (ctx.has_value()) ev = ctx.pop_value(); }
        auto* boxedPtr = box_elem(ev);
        b.CreateCall(llvm::cast<llvm::Function>(ensure_tuple_set().getCallee()), {tupAlloca, llvm::ConstantInt::get(I32, i), boxedPtr});
    }

    // Resultado é um Value (TAG_TUPLE)
    ctx.push_value(b.CreateLoad(ValueTy, tupAlloca));
}
