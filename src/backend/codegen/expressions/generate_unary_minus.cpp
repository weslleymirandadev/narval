#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void UnaryMinusExprNode::codegen(rph::IRGenerationContext& ctx) {
    if (operand) operand->codegen(ctx);
    llvm::Value* v = ctx.pop_value();
    if (!v) { ctx.push_value(nullptr); return; }
    ctx.push_value(rph::ir_utils::create_unary_op(ctx, v, "neg"));
}
