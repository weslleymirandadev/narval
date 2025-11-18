#include "frontend/ast/expressions/map_node.hpp"
#include "backend/codegen/ir_context.hpp"

void MapNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    // KeyValueNode is used only as a holder inside MapNode.
    // Its codegen is intentionally a no-op to satisfy the linker.
}
