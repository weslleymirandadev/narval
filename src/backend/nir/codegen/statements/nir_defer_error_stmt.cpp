#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/defer_error_stmt_node.hpp"

// `defer error { handler }` — intercepts errors in remaining_body.
// Pattern: wrap remaining_body in try; on error run handler_body.
void DeferErrorStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    nir_call_runtime(ctx, loc, "nv_try_push", {}, {});

    ctx.push_scope();
    for (const auto& s : remaining_body)
        if (s) s->nir_codegen(ctx);
    ctx.pop_scope();

    nir_call_runtime(ctx, loc, "nv_try_pop", {}, {});

    // Check if an error occurred; if so, run the handler.
    mlir::Value had_error = nir_call_runtime(ctx, loc, "nv_had_error", {}, {vt});
    mlir::Value cond = nir_to_i1(ctx, loc, had_error);

    auto if_op = ctx.emit_if(loc, cond);

    {
        mlir::OpBuilder::InsertionGuard g(ctx.get_builder());
        ctx.get_builder().setInsertionPointToStart(&if_op.getThenRegion().front());
        ctx.push_scope();
        mlir::Value err = nir_call_runtime(ctx, loc, "nv_get_current_error", {}, {vt});
        ctx.define("err", err);
        for (const auto& s : handler_body)
            if (s) s->nir_codegen(ctx);
        ctx.pop_scope();
        nir_terminate(if_op.getThenRegion().front(), ctx.get_builder(), loc);
    }

    if (if_op.getElseRegion().empty())
        if_op.getElseRegion().emplaceBlock();
    nir_terminate(if_op.getElseRegion().front(), ctx.get_builder(), loc);
}
