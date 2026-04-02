#include "frontend/ast/expressions/map_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"

void MapNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    
    // Registrar features usadas
    nv::register_feature("map");
    nv::register_feature("map_operations");
    
    // KeyValueNode is used only as a holder inside MapNode.
    // Its codegen is intentionally a no-op to satisfy the linker.
}
