#include "frontend/ast/statements/while_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void WhileStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    auto* func = ctx.get_current_function();
    if (!func) throw std::runtime_error("while statement outside of function");

    auto* cond_bb = llvm::BasicBlock::Create(ctx.get_context(), "while.cond", func);
    auto* body_bb = llvm::BasicBlock::Create(ctx.get_context(), "while.body", func);
    auto* exit_bb = llvm::BasicBlock::Create(ctx.get_context(), "while.exit", func);

    // Enter loop context for break/continue support
    ctx.get_control_flow().enter_loop("while", cond_bb, body_bb, cond_bb, exit_bb);

    b.CreateBr(cond_bb);
    b.SetInsertPoint(cond_bb);
    llvm::Value* cond_v = nullptr;
    if (condition) {
        condition->codegen(ctx);
        cond_v = ctx.pop_value();
    }
    if (!cond_v) cond_v = llvm::ConstantInt::getFalse(ctx.get_context());
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    if (cond_v->getType() == ValueTy) {
        auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
        auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
        auto* in = ctx.create_alloca(ValueTy, "while.cond.value");
        auto* out = ctx.create_alloca(ValueTy, "while.cond.bool");
        b.CreateStore(cond_v, in);
        auto* conv = ctx.ensure_runtime_func("nv_bool_convert", {ValuePtr, ValuePtr});
        b.CreateCall(conv, {out, in});
        auto* extract = ctx.ensure_runtime_func("extract_int_from_value", {ValuePtr}, I32);
        auto* as_i32 = b.CreateCall(extract, {out}, "while.cond.i32");
        cond_v = b.CreateICmpNE(as_i32, llvm::ConstantInt::get(I32, 0), "tobool");
    } else if (!cond_v->getType()->isIntegerTy(1)) {
        cond_v = b.CreateICmpNE(cond_v, llvm::ConstantInt::get(cond_v->getType(), 0), "tobool");
    }
    b.CreateCondBr(cond_v, body_bb, exit_bb);

    b.SetInsertPoint(body_bb);
    ctx.enter_scope();
    for (auto& stmt : body) {
        if (stmt) stmt->codegen(ctx);
    }
    if (ctx.has_value()) (void)ctx.pop_value();  // descartar valor deixado pelo body (ex.: write(...))
    ctx.exit_scope();
    // loop back to cond (continue também vai para cond_bb)
    if (!b.GetInsertBlock()->getTerminator()) {
        b.CreateBr(cond_bb);
    }

    b.SetInsertPoint(exit_bb);
    ctx.get_control_flow().exit_loop();
}
