#include "frontend/ast/expressions/string_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void StringLiteralNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.push_value(rph::ir_utils::create_string_constant(ctx, value));
}
