#include "frontend/ast/expressions/member_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"

void MemberExprNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    // TODO: implementar quando objetos/runtime estiverem prontos
    ctx.push_value(nullptr);
}
