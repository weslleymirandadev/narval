#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"

void LogicalNotExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    if (operand) operand->nir_codegen(ctx);
    mlir::Value val = ctx.pop_value();
    if (!val) { ctx.push_value({}); return; }

    ctx.push_value(nir_call_runtime(ctx, loc, "nv_value_not", {val}, {vt}));
}
