#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/post_increment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"

void PostIncrementExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();
    auto& b  = ctx.get_builder();

    if (!operand) { ctx.push_value({}); return; }

    operand->nir_codegen(ctx);
    mlir::Value old_val = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
    if (!old_val) old_val = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));

    auto one  = nir_emit_const(ctx, loc, b.getI64IntegerAttr(1));
    auto next = nir_call_runtime(ctx, loc, "nv_add", {old_val, one}, {vt});

    if (operand->kind == NodeType::Identifier) {
        auto sym = static_cast<IdentifierNode*>(operand.get())->symbol;
        ctx.define(sym, next);
    }

    // Post-increment: push the old value (before increment).
    ctx.push_value(old_val);
}
