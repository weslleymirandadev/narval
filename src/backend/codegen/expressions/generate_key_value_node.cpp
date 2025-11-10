#include "frontend/ast/expressions/key_value_node.hpp"
#include "backend/codegen/ir_context.hpp"

void KeyValueNode::codegen(rph::IRGenerationContext& ctx) {
    // KeyValueNode is used only as a holder inside MapNode.
    // Its codegen is intentionally a no-op to satisfy the linker.
}
