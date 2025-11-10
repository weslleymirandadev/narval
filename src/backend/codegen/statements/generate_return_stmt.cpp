#include "frontend/ast/statements/return_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void ReturnStmtNode::codegen(rph::IRGenerationContext& ctx) {
    if (value) {
        value->codegen(ctx);
        auto* v = ctx.pop_value();
        if (v) {
            rph::ir_utils::create_return(ctx, v);
            return;
        }
    }
    rph::ir_utils::create_return(ctx, nullptr);
}
