#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"

void AssignmentExprNode::codegen(rph::IRGenerationContext& ctx) {
    if (!target) { ctx.push_value(nullptr); return; }
    // Suporte a atribuição para access expressions: base[index] = value
    if (auto* acc = dynamic_cast<AccessExprNode*>(target.get())) {
        // Gera RHS primeiro (preservaremos para retorno)
        if (value) value->codegen(ctx);
        llvm::Value* rhs = ctx.pop_value();
        if (!rhs) { ctx.push_value(nullptr); return; }

        // Avalia base e índice
        if (acc->expr) acc->expr->codegen(ctx);
        llvm::Value* base = ctx.pop_value();
        if (acc->index) acc->index->codegen(ctx);
        llvm::Value* idx_v = ctx.pop_value();

        auto& B = ctx.get_builder();
        auto& C = ctx.get_context();
        auto& M = ctx.get_module();
        auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
        auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
        auto* I32 = llvm::Type::getInt32Ty(C);

        // Coagir índice para i32
        if (idx_v && idx_v->getType() != I32) idx_v = rph::ir_utils::promote_type(ctx, idx_v, I32);
        if (!idx_v) idx_v = llvm::ConstantInt::get(I32, 0);

        // Preparar self (Value*)
        if (!base || base->getType() != ValueTy) { ctx.push_value(nullptr); return; }
        auto* selfAlloca = ctx.create_alloca(ValueTy, "idx.self");
        B.CreateStore(base, selfAlloca);

        // Box do RHS para Value*
        auto box_arg = [&](llvm::Value* any) -> llvm::Value* {
            auto* tmp = ctx.create_alloca(ValueTy, "set.val");
            if (any->getType() == ValueTy) {
                B.CreateStore(any, tmp);
            } else if (any->getType()->isIntegerTy(1)) {
                auto decl = M.getOrInsertFunction("create_bool", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false));
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, B.CreateZExt(any, I32)});
            } else if (any->getType()->isIntegerTy()) {
                auto decl = M.getOrInsertFunction("create_int", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32}, false));
                llvm::Value* iv = any->getType()->isIntegerTy(32) ? any : B.CreateSExtOrTrunc(any, I32);
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, iv});
            } else if (any->getType()->isFloatingPointTy()) {
                auto* F64 = llvm::Type::getDoubleTy(C);
                llvm::Value* fp = any;
                if (any->getType() != F64) fp = B.CreateFPExt(any, F64);
                auto decl = M.getOrInsertFunction("create_float", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, F64}, false));
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, fp});
            } else if (any->getType() == rph::ir_utils::get_i8_ptr(ctx)) {
                auto decl = M.getOrInsertFunction("create_str", llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, rph::ir_utils::get_i8_ptr(ctx)}, false));
                B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, any});
            } else {
                B.CreateStore(llvm::UndefValue::get(ValueTy), tmp);
            }
            return tmp;
        };

        auto* rhsBox = box_arg(rhs);

        // Despacho por tag: TAG_ARRAY (5) ou TAG_VECTOR (6)
        auto selfVal = B.CreateLoad(ValueTy, selfAlloca);
        auto tag = B.CreateExtractValue(selfVal, {0});
        auto* I1 = llvm::Type::getInt1Ty(C);
        auto* isArray = B.CreateICmpEQ(tag, llvm::ConstantInt::get(tag->getType(), 5));
        auto* isVector = B.CreateICmpEQ(tag, llvm::ConstantInt::get(tag->getType(), 6));

        auto* curFn = ctx.get_current_function();
        auto* bbArr = llvm::BasicBlock::Create(C, "idx.set.array", curFn);
        auto* bbVec = llvm::BasicBlock::Create(C, "idx.set.vector", curFn);
        auto* bbMerge = llvm::BasicBlock::Create(C, "idx.set.merge", curFn);

        // Branch: prefer array if tag==5, else if tag==6 go vector, else merge
        auto* condArr = isArray;
        B.CreateCondBr(condArr, bbArr, bbVec);

        // Array path
        B.SetInsertPoint(bbArr);
        {
            auto decl = M.getOrInsertFunction(
                "array_set_index_v",
                llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32, ValuePtr}, false)
            );
            B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {selfAlloca, idx_v, rhsBox});
            B.CreateBr(bbMerge);
        }

        // Vector path
        B.SetInsertPoint(bbVec);
        {
            auto decl = M.getOrInsertFunction(
                "vector_set_method",
                llvm::FunctionType::get(llvm::Type::getVoidTy(C), {ValuePtr, I32, ValuePtr}, false)
            );
            B.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {selfAlloca, idx_v, rhsBox});
            B.CreateBr(bbMerge);
        }

        // Merge
        B.SetInsertPoint(bbMerge);
        ctx.push_value(rhs);
        return;
    }

    auto* id = dynamic_cast<IdentifierNode*>(target.get());
    if (!id) { ctx.push_value(nullptr); return; } // por enquanto só suportamos atribuição a identificadores

    if (value) value->codegen(ctx);
    llvm::Value* rhs = ctx.pop_value();
    if (!rhs) { ctx.push_value(nullptr); return; }

    auto info_opt = ctx.get_symbol_table().lookup_symbol(id->symbol);
    if (info_opt.has_value()) {
        auto info = info_opt.value();
        // promover tipo se necessário
        rhs = rph::ir_utils::promote_type(ctx, rhs, info.llvm_type);
        ctx.get_builder().CreateStore(rhs, info.value);
        ctx.push_value(rhs);
        return;
    }

    // fallback: criar variável com tipo apropriado
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    llvm::Type* chosenTy = nullptr;
    if (rhs->getType() == ValueTy) {
        chosenTy = ValueTy;
    } else {
        chosenTy = llvm::Type::getInt32Ty(ctx.get_context());
        rhs = rph::ir_utils::promote_type(ctx, rhs, chosenTy);
    }
    auto* alloca = ctx.create_and_register_variable(id->symbol, chosenTy, nullptr, false);
    ctx.get_builder().CreateStore(rhs, alloca);
    ctx.push_value(rhs);
}
