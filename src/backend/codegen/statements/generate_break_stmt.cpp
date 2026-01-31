#include "frontend/ast/statements/break_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"

void BreakStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    
    // Obter o bloco de saída do loop atual
    auto* exit_bb = ctx.get_control_flow().get_current_exit();
    if (!exit_bb) {
        throw std::runtime_error("break statement outside of loop");
    }
    
    // Branch para o bloco de saída do loop
    b.CreateBr(exit_bb);
    
    // Criar um bloco vazio após o break (nunca será executado, mas necessário para LLVM)
    auto* func = ctx.get_current_function();
    if (!func) throw std::runtime_error("break statement outside of function");
    auto* unreachable_bb = llvm::BasicBlock::Create(ctx.get_context(), "break.unreachable", func);
    b.SetInsertPoint(unreachable_bb);
}
