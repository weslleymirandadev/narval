#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/try_stmt_node.hpp"
#include <functional>

// try/catch/finally — uses the runtime setjmp/longjmp error handling.
// Pattern mirrors what the legacy codegen emits via IRGenerationContext.
void TryStatementNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();
    auto& b  = ctx.get_builder();

    // Emits a block of statements where every statement is followed by an "is an error
    // pending?" check, the rest of the block going into the `else` branch of that if.
    // The handling here is flag based (nv_save_exception, no longjmp), so without the
    // check a `throw` (or a `propagate`) in the middle of a block kept running every
    // statement after it — only the handler was deferred, not the interruption.
    // Returns the outermost if (nullptr when the block has at most one statement) and
    // leaves the builder inserted after it, i.e. at the end of the emitted block.
    auto emit_checked_block = [&](const auto& stmts) -> mlir::Operation* {
        std::function<mlir::Operation*(size_t)> emit_from = [&](size_t i) -> mlir::Operation* {
            if (i >= stmts.size()) return nullptr;
            if (stmts[i]) stmts[i]->nir_codegen(ctx);
            if (i + 1 >= stmts.size()) return nullptr;

            mlir::Value pending = nir_to_i1(ctx, loc,
                nir_call_runtime(ctx, loc, "nv_has_pending_error", {}, {vt}));
            auto if_op = ctx.emit_if(loc, pending);

            {   // error pending: nothing else in the block runs
                mlir::OpBuilder::InsertionGuard g(b);
                b.setInsertionPointToStart(&if_op.getThenRegion().front());
                nir_terminate(if_op.getThenRegion().front(), b, loc);
            }
            {   // no error: the statements that follow
                mlir::OpBuilder::InsertionGuard g(b);
                b.setInsertionPointToStart(&if_op.getElseRegion().front());
                emit_from(i + 1);
                nir_terminate(if_op.getElseRegion().front(), b, loc);
            }
            b.setInsertionPointAfter(if_op);
            return if_op;
        };
        return emit_from(0);
    };

    // Push a new error handler frame.
    nir_call_runtime(ctx, loc, "nv_try_push", {}, {});

    // Emit try body.
    ctx.push_scope();
    if (mlir::Operation* outer = emit_checked_block(try_body)) b.setInsertionPointAfter(outer);
    ctx.pop_scope();

    // Pop the handler frame (normal exit).
    nir_call_runtime(ctx, loc, "nv_try_pop", {}, {});

    // Emit catch blocks — each checks whether the thrown error matches the type.
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
            mlir::Value err_value_for_handler{};
            if (!c->exception_var.empty()) {
                mlir::Value err = nir_call_runtime(ctx, loc, "nv_get_current_error", {}, {vt});
                ctx.define(c->exception_var, err);
                err_value_for_handler = err;
            } else {
                err_value_for_handler = nir_call_runtime(ctx, loc, "nv_get_current_error", {}, {vt});
            }
            // The handler assumes the error: nv_push_handler_error saves the error it
            // is now responsible for and clears the pending flag, so the checks inside
            // the body mean "raised BY THIS handler". Without it the flag still held the
            // error just caught, the first check skipped the whole body — including the
            // `propagate` that was supposed to re-throw it — and `propagate` itself had
            // nothing to re-throw.
            nir_call_runtime(ctx, loc, "nv_push_handler_error",
                             {err_value_for_handler}, {});

            // The catch body gets the same treatment as the try body: a `propagate` in
            // the middle of it must not let the statements after it run.
            if (mlir::Operation* outer = emit_checked_block(c->body)) b.setInsertionPointAfter(outer);
            nir_call_runtime(ctx, loc, "nv_pop_handler_error", {}, {});
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

    // Finally block (always executed) — before the error is reported, so a `finally`
    // still runs when the error goes unhandled.
    for (const auto& s : finally_body)
        if (s) s->nir_codegen(ctx);

    // End of the `try`: an error that outlived the catches was raised inside a handler
    // (`propagate`, a fresh `throw`) or matched no catch. It stays pending for the
    // enclosing handler; with none outside it is reported here instead of being
    // swallowed by the unconditional clear this used to do.
    nir_call_runtime(ctx, loc, "nv_finish_try", {}, {});
}
