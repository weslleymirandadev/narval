#include "frontend/ast/expressions/increment_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"

void IncrementExprNode::codegen(nv::IRGenerationContext& ctx) {
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
        if (!info_opt.has_value()) { ctx.push_value(nullptr); return; }
        auto info = info_opt.value();
        llvm::Value* addr = info.value;
        llvm::Type* elemTy = info.llvm_type;

        auto* loaded = b.CreateLoad(elemTy, addr);
        auto* one = elemTy->isDoubleTy() ? (llvm::Value*)llvm::ConstantFP::get(elemTy, 1.0)
                                         : (llvm::Value*)llvm::ConstantInt::get(elemTy, 1);
        llvm::Value* next = elemTy->isDoubleTy() ? (llvm::Value*)b.CreateFAdd(loaded, one)
                                                 : (llvm::Value*)b.CreateAdd(loaded, one);
        b.CreateStore(next, addr);
        
        // Retornar novo valor como Value
        auto* nextBox = ctx.create_alloca(ValueTy, "next.box");
        if (elemTy->isDoubleTy()) {
            auto* f = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
            b.CreateCall(f, {nextBox, next});
        } else {
            llvm::Value* i32_val = next;
            if (next->getType() != I32) i32_val = b.CreateTrunc(next, I32);
            auto* f = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
            b.CreateCall(f, {nextBox, i32_val});
        }
        ctx.push_value(b.CreateLoad(ValueTy, nextBox));
    } else if (auto* acc = dynamic_cast<AccessExprNode*>(operand.get())) {
        // AccessExpression - usar funções runtime
        if (acc->expr) acc->expr->codegen(ctx);
        llvm::Value* base = ctx.pop_value();
        if (!base || base->getType() != ValueTy) { ctx.push_value(nullptr); return; }
        
        if (acc->index) acc->index->codegen(ctx);
        llvm::Value* idx_v = ctx.pop_value();
        if (!idx_v || idx_v->getType() != I32) idx_v = nv::ir_utils::promote_type(ctx, idx_v, I32);
        if (!idx_v) idx_v = llvm::ConstantInt::get(I32, 0);

        // Preparar self (Value*)
        auto* selfAlloca = ctx.create_alloca(ValueTy, "inc.self");
        b.CreateStore(base, selfAlloca);

        // Obter valor atual usando array_get_index_v
        auto* oldvAlloca = ctx.create_alloca(ValueTy, "inc.oldv");
        auto decl_get = m.getOrInsertFunction(
            "array_get_index_v",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, ValuePtr, I32}, false)
        );
        b.CreateCall(llvm::cast<llvm::Function>(decl_get.getCallee()), {oldvAlloca, selfAlloca, idx_v});
        llvm::Value* oldv = b.CreateLoad(ValueTy, oldvAlloca);

        // Extrair valor numérico
        auto* tmp = ctx.create_alloca(ValueTy, "inc.tmp");
        b.CreateStore(oldv, tmp);
        auto* valuePtr = b.CreateStructGEP(ValueTy, tmp, 1);
        auto* i64 = llvm::Type::getInt64Ty(c);
        auto* value64 = b.CreateLoad(i64, valuePtr);
        
        // Extrair tag para determinar tipo
        auto* tagPtr = b.CreateStructGEP(ValueTy, tmp, 0);
        auto* tag = b.CreateLoad(I32, tagPtr);
        auto* tagInt = llvm::ConstantInt::get(I32, 1); // TAG_INT
        auto* tagFloat = llvm::ConstantInt::get(I32, 2); // TAG_FLOAT
        auto* isInt = b.CreateICmpEQ(tag, tagInt);
        auto* isFloat = b.CreateICmpEQ(tag, tagFloat);

        auto* curFn = ctx.get_current_function();
        auto* bbInt = llvm::BasicBlock::Create(c, "inc.int", curFn);
        auto* bbFloat = llvm::BasicBlock::Create(c, "inc.float", curFn);
        auto* bbMerge = llvm::BasicBlock::Create(c, "inc.merge", curFn);

        b.CreateCondBr(isInt, bbInt, bbFloat);

        // Int path
        b.SetInsertPoint(bbInt);
        auto* intVal = b.CreateTrunc(value64, I32, "int.val");
        auto* intOne = llvm::ConstantInt::get(I32, 1);
        auto* intNext = b.CreateAdd(intVal, intOne, "int.next");
        auto* intNextBox = ctx.create_alloca(ValueTy, "int.next.box");
        auto* fInt = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
        b.CreateCall(fInt, {intNextBox, intNext});
        auto* intNextVal = b.CreateLoad(ValueTy, intNextBox);
        b.CreateBr(bbMerge);

        // Float path
        b.SetInsertPoint(bbFloat);
        auto* floatVal = b.CreateBitCast(value64, F64, "float.val");
        auto* floatOne = llvm::ConstantFP::get(F64, 1.0);
        auto* floatNext = b.CreateFAdd(floatVal, floatOne, "float.next");
        auto* floatNextBox = ctx.create_alloca(ValueTy, "float.next.box");
        auto* fFloat = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
        b.CreateCall(fFloat, {floatNextBox, floatNext});
        auto* floatNextVal = b.CreateLoad(ValueTy, floatNextBox);
        b.CreateBr(bbMerge);

        // Merge
        b.SetInsertPoint(bbMerge);
        auto* phi = b.CreatePHI(ValueTy, 2, "next.val");
        phi->addIncoming(intNextVal, bbInt);
        phi->addIncoming(floatNextVal, bbFloat);

        // Atualizar usando array_set_index_v ou vector_set_method
        auto selfVal = b.CreateLoad(ValueTy, selfAlloca);
        auto* tagBase = b.CreateExtractValue(selfVal, {0});
        auto* isArray = b.CreateICmpEQ(tagBase, llvm::ConstantInt::get(I32, 5));
        auto* isVector = b.CreateICmpEQ(tagBase, llvm::ConstantInt::get(I32, 6));

        auto* bbArr = llvm::BasicBlock::Create(c, "inc.set.array", curFn);
        auto* bbVec = llvm::BasicBlock::Create(c, "inc.set.vector", curFn);
        auto* bbSetMerge = llvm::BasicBlock::Create(c, "inc.set.merge", curFn);

        b.CreateCondBr(isArray, bbArr, bbVec);

        // Array path
        b.SetInsertPoint(bbArr);
        {
            auto* nextBox = ctx.create_alloca(ValueTy, "next.box");
            b.CreateStore(phi, nextBox);
            auto decl = m.getOrInsertFunction(
                "array_set_index_v",
                llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I32, ValuePtr}, false)
            );
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {selfAlloca, idx_v, nextBox});
            b.CreateBr(bbSetMerge);
        }

        // Vector path
        b.SetInsertPoint(bbVec);
        {
            auto* nextBox = ctx.create_alloca(ValueTy, "next.box");
            b.CreateStore(phi, nextBox);
            auto decl = m.getOrInsertFunction(
                "vector_set_method",
                llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I32, ValuePtr}, false)
            );
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {selfAlloca, idx_v, nextBox});
            b.CreateBr(bbSetMerge);
        }

        // Set merge
        b.SetInsertPoint(bbSetMerge);
        ctx.push_value(phi); // Retornar novo valor
    } else {
        ctx.push_value(nullptr);
    }
}
