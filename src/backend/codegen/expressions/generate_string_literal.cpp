#include "frontend/ast/expressions/string_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"

void StringLiteralNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    
    // Registrar features usadas
<<<<<<< HEAD
    nv::register_feature("str");
=======
    nv::register_feature("string");
>>>>>>> 7d7b28c04a119a9c000597cd586b6688408f92d1
    nv::register_feature("string_operations");
    
    // TEMP: Voltar para versão simples que funcionava
    auto* str_ptr = nv::ir_utils::create_string_constant(ctx, value);
    ctx.push_value(str_ptr);
}