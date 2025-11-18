#include "frontend/ast/statements/loop_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"

void LoopStmtNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    auto* func = ctx.get_current_function();
    if (!func) throw std::runtime_error("loop statement outside of function");

    auto* header_bb   = llvm::BasicBlock::Create(ctx.get_context(), "loop.header", func);
    auto* body_bb     = llvm::BasicBlock::Create(ctx.get_context(), "loop.body", func);
    auto* continue_bb = llvm::BasicBlock::Create(ctx.get_context(), "loop.continue", func);
    auto* exit_bb     = llvm::BasicBlock::Create(ctx.get_context(), "loop.exit", func);

    // Enter loop context for potential break/continue support
    ctx.get_control_flow().enter_loop("loop", header_bb, body_bb, continue_bb, exit_bb);

    // jump to header
    b.CreateBr(header_bb);

    // header just falls through to body (infinite loop)
    b.SetInsertPoint(header_bb);
    b.CreateBr(body_bb);

    // body
    b.SetInsertPoint(body_bb);
    ctx.enter_scope();
    for (auto& stmt : body) {
        if (stmt) stmt->codegen(ctx);
    }
    ctx.exit_scope();
    b.CreateBr(continue_bb);

    // continue: go back to header
    b.SetInsertPoint(continue_bb);
    b.CreateBr(header_bb);

    // exit point (reachable by break if implemented elsewhere)
    b.SetInsertPoint(exit_bb);
    ctx.get_control_flow().exit_loop();
}
