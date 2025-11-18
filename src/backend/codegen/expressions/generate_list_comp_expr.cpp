#include "frontend/ast/expressions/list_comp_node.hpp"
#include "backend/codegen/ir_context.hpp"

void ListCompNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    // TODO: implementar quando semântica e runtime para compreensões estiverem definidas
    ctx.push_value(nullptr);
}
