#include "frontend/ast/statements/continue_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"

void ContinueStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    
    // Obter o bloco de continue do loop atual
    auto* continue_bb = ctx.get_control_flow().get_current_continue();
    if (!continue_bb) {
        // Se não houver bloco de continue específico, usar o header do loop
        auto loop_ctx = ctx.get_control_flow().get_current_loop();
        if (!loop_ctx.has_value()) {
            throw std::runtime_error("continue statement outside of loop");
        }
        continue_bb = loop_ctx->loop_header;
    }
    
    // Branch para o bloco de continue (ou header) do loop
    b.CreateBr(continue_bb);
    
    // Criar um bloco vazio após o continue (nunca será executado, mas necessário para LLVM)
    auto* func = ctx.get_current_function();
    if (!func) throw std::runtime_error("continue statement outside of function");
    auto* unreachable_bb = llvm::BasicBlock::Create(ctx.get_context(), "continue.unreachable", func);
    b.SetInsertPoint(unreachable_bb);
}
