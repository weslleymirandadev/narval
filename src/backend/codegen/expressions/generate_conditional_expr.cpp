#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void ConditionalExprNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    auto* func = ctx.get_current_function();
    if (!func) { ctx.push_value(nullptr); return; }

    // cond
    if (condition) condition->codegen(ctx);
    llvm::Value* cond_v = ctx.pop_value();
    if (!cond_v) cond_v = llvm::ConstantInt::getFalse(ctx.get_context());
    if (!cond_v->getType()->isIntegerTy(1)) {
        cond_v = b.CreateICmpNE(cond_v, llvm::ConstantInt::get(cond_v->getType(), 0), "tobool");
    }

    // blocks
    auto* then_bb  = llvm::BasicBlock::Create(ctx.get_context(), "cond.then", func);
    auto* else_bb  = llvm::BasicBlock::Create(ctx.get_context(), "cond.else", func);
    auto* merge_bb = llvm::BasicBlock::Create(ctx.get_context(), "cond.end",  func);
    b.CreateCondBr(cond_v, then_bb, else_bb);

    // then
    b.SetInsertPoint(then_bb);
    if (true_expr) true_expr->codegen(ctx);
    llvm::Value* tv = ctx.pop_value();
    if (!tv) tv = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx.get_context()), 0);
    tv = rph::ir_utils::promote_type(ctx, tv, llvm::Type::getInt32Ty(ctx.get_context()));
    b.CreateBr(merge_bb);
    auto* then_end = b.GetInsertBlock();

    // else
    b.SetInsertPoint(else_bb);
    if (false_expr) false_expr->codegen(ctx);
    llvm::Value* fv = ctx.pop_value();
    if (!fv) fv = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx.get_context()), 0);
    fv = rph::ir_utils::promote_type(ctx, fv, llvm::Type::getInt32Ty(ctx.get_context()));
    b.CreateBr(merge_bb);
    auto* else_end = b.GetInsertBlock();

    // merge with PHI
    b.SetInsertPoint(merge_bb);
    auto* phi = b.CreatePHI(llvm::Type::getInt32Ty(ctx.get_context()), 2, "condphi");
    phi->addIncoming(tv, then_end);
    phi->addIncoming(fv, else_end);
    ctx.push_value(phi);
}
