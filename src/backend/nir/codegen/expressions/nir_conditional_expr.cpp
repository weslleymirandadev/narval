#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"

// Ternary: `true_expr if condition else false_expr`
// Lowered by evaluating BOTH branches eagerly and selecting between them
// using a runtime nv_select(cond, a, b) function. This avoids narval.if
// with result values, which simplifies the lowering pipeline.
void ConditionalExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();

    // Evaluate condition
    if (condition) condition->nir_codegen(ctx);
    mlir::Value cond_val = ctx.pop_value();
    if (!cond_val) cond_val = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
    mlir::Value i1_cond = nir_to_i1(ctx, loc, cond_val);

    // Evaluate true_expr eagerly
    mlir::Value tv;
    if (true_expr) { true_expr->nir_codegen(ctx); tv = ctx.pop_value(); }
    if (!tv) tv = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));

    // Evaluate false_expr eagerly
    mlir::Value fv;
    if (false_expr) { false_expr->nir_codegen(ctx); fv = ctx.pop_value(); }
    if (!fv) fv = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));

    // Use nv_select(i1, a, b) → value runtime helper
    auto i1 = b.getI1Type();
    ctx.ensure_runtime_func("nv_select",
        mlir::FunctionType::get(&ctx.get_mlir_context(), {i1, vt, vt}, {vt}));
    auto call = mlir::narval::CallRuntimeOp::create(
        b, loc, mlir::TypeRange{vt},
        mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_select"),
        mlir::ValueRange{i1_cond, tv, fv});
    ctx.push_value(call.getResults()[0]);
}
