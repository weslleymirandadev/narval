#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"

void AccessExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    if (expr)  expr->nir_codegen(ctx);
    mlir::Value base = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
    if (index) index->nir_codegen(ctx);
    mlir::Value idx  = ctx.has_value() ? ctx.pop_value() : mlir::Value{};

    if (!base) base = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));
    if (!idx)  idx  = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));

    ctx.push_value(nir_call_runtime(ctx, loc, "nv_array_get", {base, idx}, {vt}));
}
