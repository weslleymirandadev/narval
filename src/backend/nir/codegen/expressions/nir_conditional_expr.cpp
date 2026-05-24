#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"

// Ternary: `true_expr if condition else false_expr`
// Lowered as narval.if that yields a single !narval.value.
void ConditionalExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();

    // Evaluate condition
    if (condition) condition->nir_codegen(ctx);
    mlir::Value cond = ctx.pop_value();
    if (!cond) cond = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
    mlir::Value i1_cond = nir_to_i1(ctx, loc, cond);

    auto if_op = ctx.emit_if(loc, i1_cond, {vt});

    // then-branch: evaluate true_expr, yield its value
    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&if_op.getThenRegion().front());
        if (true_expr) true_expr->nir_codegen(ctx);
        mlir::Value tv = ctx.pop_value();
        if (!tv) tv = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
        mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{tv});
    }

    // else-branch: evaluate false_expr, yield its value
    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&if_op.getElseRegion().front());
        if (false_expr) false_expr->nir_codegen(ctx);
        mlir::Value fv = ctx.pop_value();
        if (!fv) fv = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
        mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{fv});
    }

    b.setInsertionPointAfter(if_op);
    ctx.push_value(if_op.getResult(0));
}
