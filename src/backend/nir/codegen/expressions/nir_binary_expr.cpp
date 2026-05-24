#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"

void BinaryExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    // ── Tensor ops — emit narval dialect ops directly ──────────────────────
    if (!tensor_op.empty()) {
        if (left)  left->nir_codegen(ctx);
        if (right) right->nir_codegen(ctx);
        mlir::Value rhs = ctx.pop_value();
        mlir::Value lhs = ctx.pop_value();
        if (!lhs || !rhs) { ctx.push_value({}); return; }

        mlir::Value result;
        auto& b = ctx.get_builder();
        if (tensor_op == "matmul")
            result = mlir::narval::TensorMatmulOp::create(b, loc, vt, lhs, rhs).getResult();
        else if (tensor_op == "add")
            result = mlir::narval::TensorAddOp::create(b, loc, vt, lhs, rhs).getResult();
        else if (tensor_op == "mul" || tensor_op == "scalar_mul")
            result = mlir::narval::TensorMulOp::create(b, loc, vt, lhs, rhs).getResult();
        else
            result = nir_call_runtime(ctx, loc, "nv_add", {lhs, rhs}, {vt});

        ctx.push_value(result);
        return;
    }

    // ── Standard binary ops ────────────────────────────────────────────────
    if (left)  left->nir_codegen(ctx);
    if (right) right->nir_codegen(ctx);
    mlir::Value rhs = ctx.pop_value();
    mlir::Value lhs = ctx.pop_value();
    if (!lhs || !rhs) { ctx.push_value({}); return; }

    llvm::StringRef fn;
    if      (op == "+")              fn = "nv_add";
    else if (op == "-")              fn = "nv_sub";
    else if (op == "*")              fn = "nv_mul";
    else if (op == "/")              fn = "nv_div";
    else if (op == "%")              fn = "nv_mod";
    else if (op == "==")             fn = "nv_value_eq";
    else if (op == "!=")             fn = "nv_value_ne";
    else if (op == "<")              fn = "nv_value_lt";
    else if (op == "<=")             fn = "nv_value_le";
    else if (op == ">")              fn = "nv_value_gt";
    else if (op == ">=")             fn = "nv_value_ge";
    else if (op == "&&" || op == "and") fn = "nv_value_and";
    else if (op == "||" || op == "or")  fn = "nv_value_or";
    else                             fn = "nv_add";

    ctx.push_value(nir_call_runtime(ctx, loc, fn, {lhs, rhs}, {vt}));
}
