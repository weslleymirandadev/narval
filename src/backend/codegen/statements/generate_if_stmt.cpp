#include "frontend/ast/statements/if_statement_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void IfStatementNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    llvm::DIBuilder* dib = ctx.get_debug_builder();
    llvm::DIFile* dif = ctx.get_debug_file();
    llvm::DIScope* old_scope = ctx.get_debug_scope();
    llvm::DIScope* if_scope = old_scope;

    if (dib && dif && position) {
        if_scope = dib->createLexicalBlock(
            old_scope ? old_scope : static_cast<llvm::DIScope*>(dif),
            dif,
            static_cast<unsigned>(position->line),
            static_cast<unsigned>(position->col[0] + 1)
        );
        ctx.set_debug_scope(if_scope);
    }

    if (!condition) throw std::runtime_error("if statement without condition");
    condition->codegen(ctx);
    auto* cond_v = ctx.pop_value();
    if (!cond_v) throw std::runtime_error("if statement without condition value");

    // Ensure condition is i1
    if (!cond_v->getType()->isIntegerTy(1)) {
        cond_v = ctx.get_builder().CreateICmpNE(
            cond_v,
            llvm::ConstantInt::get(cond_v->getType(), 0),
            "tobool"
        );
    }

    auto blocks = rph::ir_utils::create_if_else_structure(ctx, "if");
    rph::ir_utils::create_conditional_branch(ctx, cond_v, blocks.then_block, blocks.else_block);

    auto& B = ctx.get_builder();

    // Then block
    B.SetInsertPoint(blocks.then_block);
    ctx.enter_scope();
    for (auto& stmt : consequent) {
        if (stmt) stmt->codegen(ctx);
    }
    ctx.exit_scope();
    if (!B.GetInsertBlock()->getTerminator()) {
        B.CreateBr(blocks.merge_block);
    }

    bool has_else = !alternate.empty();

    // Else/Elif chain emission helper: emits a chain of elifs and an optional final else
    // All successful branches must end in a branch to the provided merge block.
    auto emit_else_chain = [&](auto&& self, size_t idx, llvm::BasicBlock* current_else_block, llvm::BasicBlock* merge_block) -> void {
        B.SetInsertPoint(current_else_block);

        // No more alternate statements: empty else
        if (idx >= alternate.size()) {
            if (!B.GetInsertBlock()->getTerminator()) {
                B.CreateBr(merge_block);
            }
            return;
        }

        // If the next alternate is an IfStatementNode, treat as elif
        if (auto* elif_node = dynamic_cast<IfStatementNode*>(alternate[idx].get())) {
            // Generate condition for elif
            elif_node->condition->codegen(ctx);
            auto* elif_cond = ctx.pop_value();
            if (!elif_cond) throw std::runtime_error("elif without condition value");
            if (!elif_cond->getType()->isIntegerTy(1)) {
                elif_cond = B.CreateICmpNE(
                    elif_cond,
                    llvm::ConstantInt::get(elif_cond->getType(), 0),
                    "tobool"
                );
            }

            // Create blocks for this elif
            auto elif_blocks = rph::ir_utils::create_if_else_structure(ctx, "elif");
            rph::ir_utils::create_conditional_branch(ctx, elif_cond, elif_blocks.then_block, elif_blocks.else_block);

            // Elif then: execute its consequent, then jump to merge
            B.SetInsertPoint(elif_blocks.then_block);
            ctx.enter_scope();
            for (auto& s : elif_node->consequent) {
                if (s) s->codegen(ctx);
            }
            ctx.exit_scope();
            if (!B.GetInsertBlock()->getTerminator()) {
                B.CreateBr(merge_block);
            }

            // Elif else: continue with the rest of the chain (either another elif or final else)
            self(self, idx + 1, elif_blocks.else_block, merge_block);
            return;
        }

        // Otherwise, remaining statements constitute the final else body
        ctx.enter_scope();
        for (size_t i = idx; i < alternate.size(); ++i) {
            if (alternate[i]) alternate[i]->codegen(ctx);
        }
        ctx.exit_scope();
        if (!B.GetInsertBlock()->getTerminator()) {
            B.CreateBr(merge_block);
        }
    };

    // Else block (could be empty, elif-chain, or final else)
    if (has_else) {
        emit_else_chain(emit_else_chain, 0, blocks.else_block, blocks.merge_block);
    } else {
        B.SetInsertPoint(blocks.else_block);
        if (!B.GetInsertBlock()->getTerminator()) {
            B.CreateBr(blocks.merge_block);
        }
    }

    // Restore debug scope
    if (dib && dif && position) {
        ctx.set_debug_scope(old_scope);
    }

    // Continue at merge
    B.SetInsertPoint(blocks.merge_block);
}
