#include "frontend/ast/expressions/boolean_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void BooleanLiteralNode::codegen(nv::IRGenerationContext& context) {
    context.set_debug_location(position.get());
    context.push_value(nv::ir_utils::create_bool_constant(context, value));
}