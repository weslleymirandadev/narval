#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/await_expr_node.hpp"

void AwaitExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();
    if (operand) operand->nir_codegen(ctx);
    mlir::Value fut = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
    if (!fut) fut = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));
    ctx.push_value(nir_call_runtime(ctx, loc, "nv_await_fiber", {fut}, {vt}));
}
