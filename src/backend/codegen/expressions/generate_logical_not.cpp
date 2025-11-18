#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void LogicalNotExprNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    if (operand) operand->codegen(ctx);
    llvm::Value* v = ctx.pop_value();
    if (!v) { ctx.push_value(nullptr); return; }
    ctx.push_value(rph::ir_utils::create_unary_op(ctx, v, "not"));
}
