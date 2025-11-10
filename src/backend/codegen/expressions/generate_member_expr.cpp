#include "frontend/ast/expressions/member_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"

void MemberExprNode::codegen(rph::IRGenerationContext& ctx) {
    // TODO: implementar quando objetos/runtime estiverem prontos
    ctx.push_value(nullptr);
}
