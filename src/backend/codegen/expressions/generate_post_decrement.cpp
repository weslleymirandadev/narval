#include "frontend/ast/expressions/post_decrement_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"

void PostDecrementExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    auto& c = ctx.get_context();
    auto& m = ctx.get_module();
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I32 = llvm::Type::getInt32Ty(c);
    auto* F64 = llvm::Type::getDoubleTy(c);

    if (auto* id = dynamic_cast<IdentifierNode*>(operand.get())) {
        // Identificador simples - usar código original
        auto info_opt = ctx.get_symbol_table().lookup_symbol(id->symbol);
        if (!info_opt.has_value()) {
            ctx.push_value(nullptr);
            return;
        }
        auto info = info_opt.value();
        llvm::Value* addr = info.value;
        llvm::Type* elemTy = info.llvm_type;

        auto* loaded = b.CreateLoad(elemTy, addr);
        auto* one = elemTy->isDoubleTy() ? (llvm::Value*)llvm::ConstantFP::get(elemTy, 1.0)
                                       : (llvm::Value*)llvm::ConstantInt::get(elemTy, 1);
        llvm::Value* next = elemTy->isDoubleTy() ? (llvm::Value*)b.CreateFSub(loaded, one)
                                                 : (llvm::Value*)b.CreateSub(loaded, one);
        b.CreateStore(next, addr);

        ctx.push_value(next);
        return;
    }

    // Access expression (array/vector)
    if (auto* acc = dynamic_cast<AccessExprNode*>(operand.get())) {
        // Gerar código para o self (array/vector)
        acc->expr->codegen(ctx);
        llvm::Value* base = ctx.pop_value();
        if (!base || base->getType() != ValueTy) return;

        // Gerar código para o índice
        if (acc->index) acc->index->codegen(ctx);
        llvm::Value* idx_v = ctx.pop_value();
        if (!idx_v || idx_v->getType() != I32) idx_v = nv::ir_utils::promote_type(ctx, idx_v, I32);
        if (!idx_v) idx_v = llvm::ConstantInt::get(I32, 0);

        // Preparar self (Value*)
        auto* selfAlloca = ctx.create_alloca(ValueTy, "pdec.self");
        b.CreateStore(base, selfAlloca);

        // Obter valor atual usando array_get_index_v
        auto* oldvAlloca = ctx.create_alloca(ValueTy, "pdec.oldv");
        auto decl_get = m.getOrInsertFunction(
            "array_get_index_v",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, ValuePtr, I32}, false)
        );
        b.CreateCall(llvm::cast<llvm::Function>(decl_get.getCallee()), {oldvAlloca, selfAlloca, idx_v});
        llvm::Value* oldv = b.CreateLoad(ValueTy, oldvAlloca);

        // Extrair valor numérico usando funções runtime
        auto* tmp = ctx.create_alloca(ValueTy, "pdec.tmp");
        b.CreateStore(oldv, tmp);
        
        // Usar função runtime para extrair int
        auto* extract_int_func = ctx.ensure_runtime_func("extract_int_from_value", {I32, ValuePtr});
        auto* value64 = b.CreateCall(extract_int_func, {tmp}, "value64");
        auto* i64 = llvm::Type::getInt64Ty(c);
        auto* value64_cast = b.CreateSExt(value64, i64, "value64_cast");
        
        auto* intVal = b.CreateTrunc(value64_cast, I32, "int.val");
        auto* intOne = llvm::ConstantInt::get(I32, 1);
        auto* intNext = b.CreateSub(intVal, intOne, "int.next");
        auto* intNextBox = ctx.create_alloca(ValueTy, "int.next.box");
        auto* fInt = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
        b.CreateCall(fInt, {intNextBox, intNext});
        auto* intNextVal = b.CreateLoad(ValueTy, intNextBox);

        // Atualizar usando array_set_index_v
        auto* nextBox = ctx.create_alloca(ValueTy, "pdec.next.box");
        b.CreateStore(intNextVal, nextBox);
        auto decl = m.getOrInsertFunction(
            "array_set_index_v",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I32, ValuePtr}, false)
        );
        b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {selfAlloca, idx_v, nextBox});

        ctx.push_value(intNextVal);
        return;
    }
}
