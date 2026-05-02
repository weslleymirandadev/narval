#include "frontend/ast/expressions/self_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void SelfExprNode::codegen(nv::IRGenerationContext& ctx) {
    auto info = ctx.get_symbol_table().lookup_symbol("__self");
    if (!info.has_value()) {
        ctx.push_value(nullptr);
        return;
    }
    auto& B = ctx.get_builder();
    auto* loaded = B.CreateLoad(nv::ir_utils::get_value_struct(ctx), info.value().value, "this_val");
    ctx.push_value(loaded);
}
