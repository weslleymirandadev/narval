#include "frontend/ast/statements/return_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void ReturnStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    if (value) {
        value->codegen(ctx);
        auto* v = ctx.pop_value();
        if (v) {
            nv::ir_utils::create_return(ctx, v);
            return;
        }
    }
    nv::ir_utils::create_return(ctx, nullptr);
}
