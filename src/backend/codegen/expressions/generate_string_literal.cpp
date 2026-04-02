#include "frontend/ast/expressions/string_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"

void StringLiteralNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    
    // Registrar features usadas
    nv::register_feature("string");
    nv::register_feature("string_operations");
    
    ctx.push_value(nv::ir_utils::create_string_constant(ctx, value));
}