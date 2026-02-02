#include "frontend/ast/expressions/param_node.hpp"
#include "backend/codegen/ir_context.hpp"

void ParamNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    // ParamNode is a metadata holder for def parameters; no IR to emit.
    // Push a placeholder to keep stack discipline if ever used in expressions.
    ctx.push_value(nullptr);
}
