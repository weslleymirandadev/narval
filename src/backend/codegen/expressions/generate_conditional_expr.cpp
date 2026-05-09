#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

// Box any LLVM value into a Value struct slot via runtime calls
static void box_into_slot(nv::IRGenerationContext& ctx, llvm::Value* slot, llvm::Value* val) {
    if (!val) return;
    auto& b        = ctx.get_builder();
    auto& C        = ctx.get_context();
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I32      = llvm::Type::getInt32Ty(C);
    auto* F64      = llvm::Type::getDoubleTy(C);
    auto* I8P      = nv::ir_utils::get_i8_ptr(ctx);

    if (val->getType() == ValueTy) {
        b.CreateStore(val, slot);
    } else if (val->getType()->isIntegerTy(1)) {
        auto* fn = ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
        b.CreateCall(fn, {slot, b.CreateZExt(val, I32)});
    } else if (val->getType()->isIntegerTy()) {
        auto* fn = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
        auto* iv = val->getType()->isIntegerTy(32) ? val : b.CreateSExtOrTrunc(val, I32);
        b.CreateCall(fn, {slot, iv});
    } else if (val->getType()->isFloatingPointTy()) {
        auto* fn = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
        auto* fv = val->getType() == F64 ? val : b.CreateFPExt(val, F64);
        b.CreateCall(fn, {slot, fv});
    } else if (val->getType() == I8P) {
        auto* fn = ctx.ensure_runtime_func("create_str", {ValuePtr, I8P});
        b.CreateCall(fn, {slot, val});
    }
}

void ConditionalExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    auto& C = ctx.get_context();
    auto* func = ctx.get_current_function();
    if (!func) { ctx.push_value(nullptr); return; }

    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);

    // Result slot — avoids PHI type mismatch
    auto* result_slot = ctx.create_alloca(ValueTy, "cond.result");

    // Evaluate condition
    if (condition) condition->codegen(ctx);
    llvm::Value* cond_v = ctx.pop_value();
    if (!cond_v) cond_v = llvm::ConstantInt::getFalse(C);
    if (!cond_v->getType()->isIntegerTy(1)) {
        cond_v = b.CreateICmpNE(cond_v,
            llvm::ConstantInt::get(cond_v->getType(), 0), "tobool");
    }

    auto* then_bb  = llvm::BasicBlock::Create(C, "cond.then",  func);
    auto* else_bb  = llvm::BasicBlock::Create(C, "cond.else",  func);
    auto* merge_bb = llvm::BasicBlock::Create(C, "cond.merge", func);
    b.CreateCondBr(cond_v, then_bb, else_bb);

    b.SetInsertPoint(then_bb);
    if (true_expr) true_expr->codegen(ctx);
    box_into_slot(ctx, result_slot, ctx.has_value() ? ctx.pop_value() : nullptr);
    b.CreateBr(merge_bb);

    b.SetInsertPoint(else_bb);
    if (false_expr) false_expr->codegen(ctx);
    box_into_slot(ctx, result_slot, ctx.has_value() ? ctx.pop_value() : nullptr);
    b.CreateBr(merge_bb);

    b.SetInsertPoint(merge_bb);
    ctx.push_value(b.CreateLoad(ValueTy, result_slot, "cond.val"));
}
