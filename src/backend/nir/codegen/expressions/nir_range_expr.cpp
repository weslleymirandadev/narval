#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"

void RangeExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();
    auto& b  = ctx.get_builder();

    if (start) start->nir_codegen(ctx);
    mlir::Value s = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
    if (end)   end->nir_codegen(ctx);
    mlir::Value e = ctx.has_value() ? ctx.pop_value() : mlir::Value{};

    if (!s) s = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
    if (!e) e = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));

    auto inc_flag = nir_emit_const(ctx, loc, b.getI64IntegerAttr(inclusive ? 1 : 0));
    ctx.push_value(nir_call_runtime(ctx, loc, "nv_make_range", {s, e, inc_flag}, {vt}));
}
