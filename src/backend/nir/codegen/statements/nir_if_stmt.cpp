#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"

void IfStatementNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());

    mlir::Value cond;
    if (condition) {
        condition->nir_codegen(ctx);
        cond = ctx.pop_value();
    }

    if (!cond) {
        nir_emit_body(consequent, ctx);
        return;
    }

    mlir::Value i1_cond = nir_to_i1(ctx, loc, cond);
    bool has_else = !alternate.empty();
    auto if_op = ctx.emit_if(loc, i1_cond, {});

    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&if_op.getThenRegion().front());
        ctx.push_scope();
        nir_emit_body(consequent, ctx);
        ctx.pop_scope();
        nir_terminate(if_op.getThenRegion().front(), b, loc);
    }

    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&if_op.getElseRegion().front());
        if (has_else) {
            ctx.push_scope();
            nir_emit_body(alternate, ctx);
            ctx.pop_scope();
        }
        nir_terminate(if_op.getElseRegion().front(), b, loc);
    }

    b.setInsertionPointAfter(if_op);
}
