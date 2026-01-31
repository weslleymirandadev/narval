#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void BinaryExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    if (left) left->codegen(ctx);
    auto* lhs_v = ctx.pop_value();
    if (right) right->codegen(ctx);
    auto* rhs_v = ctx.pop_value();
    if (!lhs_v || !rhs_v) { ctx.push_value(nullptr); return; }

    // Logical AND / OR (assume i1 operands; if not, compare != 0)
    if (op == "&&") {
        auto& b = ctx.get_builder();
        if (!lhs_v->getType()->isIntegerTy(1)) lhs_v = b.CreateICmpNE(lhs_v, llvm::ConstantInt::get(lhs_v->getType(), 0));
        if (!rhs_v->getType()->isIntegerTy(1)) rhs_v = b.CreateICmpNE(rhs_v, llvm::ConstantInt::get(rhs_v->getType(), 0));
        ctx.push_value(b.CreateAnd(lhs_v, rhs_v, "land"));
        return;
    }
    if (op == "||") {
        auto& b = ctx.get_builder();
        if (!lhs_v->getType()->isIntegerTy(1)) lhs_v = b.CreateICmpNE(lhs_v, llvm::ConstantInt::get(lhs_v->getType(), 0));
        if (!rhs_v->getType()->isIntegerTy(1)) rhs_v = b.CreateICmpNE(rhs_v, llvm::ConstantInt::get(rhs_v->getType(), 0));
        ctx.push_value(b.CreateOr(lhs_v, rhs_v, "lor"));
        return;
    }

    // Comparisons
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        ctx.push_value(nv::ir_utils::create_comparison(ctx, lhs_v, rhs_v, op));
        return;
    }

    // Arithmetic
    ctx.push_value(nv::ir_utils::create_binary_op(ctx, lhs_v, rhs_v, op));
}
