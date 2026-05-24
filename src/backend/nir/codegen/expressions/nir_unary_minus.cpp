#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"

void UnaryMinusExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    if (operand) operand->nir_codegen(ctx);
    mlir::Value val = ctx.pop_value();
    if (!val) { ctx.push_value({}); return; }

    auto zero = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));
    ctx.push_value(nir_call_runtime(ctx, loc, "nv_sub", {zero, val}, {vt}));
}
