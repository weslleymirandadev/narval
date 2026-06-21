#include "../nir_codegen_utils.hpp"
#include "backend/nir/nir_tensor_codegen.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"

void MemberExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();

    if (!object) { ctx.push_value({}); return; }
    object->nir_codegen(ctx);
    mlir::Value obj = ctx.pop_value();
    if (!obj) { ctx.push_value({}); return; }

    // Property must be a plain identifier for static field access
    if (!property || property->kind != NodeType::Identifier) {
        ctx.push_value(obj);
        return;
    }

    auto field = static_cast<IdentifierNode*>(property.get())->symbol;

    // ── Tensor property access ─────────────────────────────────────────
    if (nv_tensor_codegen::try_handle_member(ctx, obj, field))
        return;

    // ── Standard object field access ──────────────────────────────────
    auto result = mlir::narval::GetFieldOp::create(
        ctx.get_builder(), loc, vt, obj,
        mlir::StringAttr::get(&ctx.get_mlir_context(), field)).getResult();
    ctx.push_value(result);
}
