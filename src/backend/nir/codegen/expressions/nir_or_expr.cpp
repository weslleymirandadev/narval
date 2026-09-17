#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/or_expr_node.hpp"

// `expr or fallback`
// If expr is not None/Err, yield expr; otherwise yield fallback (or run block).
// In NIR, this lowers as:
//   cond = nv_value_is_some_or_ok(expr)
//   narval.if cond → yield expr
//              else → yield fallback / run block
void OrExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();

    // Evaluate the base expression
    if (expr) expr->nir_codegen(ctx);
    mlir::Value base = ctx.pop_value();
    if (!base) base = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));

    // Check if base is Some/Ok (not None/Err)
    auto i1 = b.getI1Type();
    mlir::Value is_valid = nir_call_runtime(ctx, loc, "nv_value_is_some_or_ok",
                                             {base}, {i1});

    auto if_op = ctx.emit_if(loc, is_valid, {vt});

    // then-branch: value is valid → yield base
    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&if_op.getThenRegion().front());
        mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{base});
    }

    // else-branch: use fallback
    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&if_op.getElseRegion().front());
        mlir::Value fallback;
        if (is_block_handler) {
            ctx.push_scope();
            // `err` is the checker's binding for the error the base expression
            // carries, and the handler may read it — without this definition the
            // identifier was unbound (warning + 0) and `x = Err("bad") or { err };`
            // bound 0.
            ctx.define("err", nir_call_runtime(ctx, loc, "nv_or_error", {base}, {vt}));
            nir_emit_body(block_stmts, ctx);
            ctx.pop_scope();
            fallback = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
        } else if (value_handler) {
            ctx.push_scope();
            ctx.define("err", nir_call_runtime(ctx, loc, "nv_or_error", {base}, {vt}));
            value_handler->nir_codegen(ctx);
            ctx.pop_scope();
            fallback = ctx.pop_value();
        }
        if (!fallback) fallback = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
        mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{fallback});
    }

    b.setInsertionPointAfter(if_op);
    ctx.push_value(if_op.getResult(0));
}
