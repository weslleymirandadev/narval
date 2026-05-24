#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/try_stmt_node.hpp"

// try/catch/finally — uses the runtime setjmp/longjmp error handling.
// Pattern mirrors what the legacy codegen emits via IRGenerationContext.
void TryStatementNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    // Push a new error handler frame.
    nir_call_runtime(ctx, loc, "nv_try_push", {}, {});

    // Emit try body.
    ctx.push_scope();
    for (const auto& s : try_body)
        if (s) s->nir_codegen(ctx);
    ctx.pop_scope();

    // Pop the handler frame (normal exit).
    nir_call_runtime(ctx, loc, "nv_try_pop", {}, {});

    // Emit catch blocks — each checks whether the thrown error matches the type.
    auto& b = ctx.get_builder();
    for (const auto& c : catches) {
        auto type_val = mlir::narval::ConstantOp::create(
            b, loc, vt,
            mlir::StringAttr::get(&ctx.get_mlir_context(), c->exception_type)).getResult();
        mlir::Value matched = nir_call_runtime(ctx, loc, "nv_catch_check", {type_val}, {vt});
        mlir::Value cond    = nir_to_i1(ctx, loc, matched);

        auto if_op = ctx.emit_if(loc, cond);

        {
            mlir::OpBuilder::InsertionGuard g(b);
            b.setInsertionPointToStart(&if_op.getThenRegion().front());
            ctx.push_scope();
            if (!c->exception_var.empty()) {
                mlir::Value err = nir_call_runtime(ctx, loc, "nv_get_current_error", {}, {vt});
                ctx.define(c->exception_var, err);
            }
            for (const auto& s : c->body)
                if (s) s->nir_codegen(ctx);
            ctx.pop_scope();
            nir_terminate(if_op.getThenRegion().front(), b, loc);
        }

        {
            mlir::OpBuilder::InsertionGuard g(b);
            b.setInsertionPointToStart(&if_op.getElseRegion().front());
            nir_terminate(if_op.getElseRegion().front(), b, loc);
        }

        b.setInsertionPointAfter(if_op);
    }

    // Clear the exception after all catch blocks (matched or not).
    nir_call_runtime(ctx, loc, "nv_clear_current_exception", {}, {});

    // Finally block (always executed).
    for (const auto& s : finally_body)
        if (s) s->nir_codegen(ctx);
}
