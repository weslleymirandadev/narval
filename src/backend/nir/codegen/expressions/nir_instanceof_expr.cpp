#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/instanceof_expr_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

void InstanceofExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    if (object) object->nir_codegen(ctx);
    mlir::Value obj = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
    if (!obj) obj = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));

    // Emit a global string for the class name.
    auto& b   = ctx.get_builder();
    auto& mlir_ctx = ctx.get_mlir_context();
    auto name_attr = mlir::StringAttr::get(&mlir_ctx, class_name);
    // Use narval.constant with a string attribute for the class name.
    auto name_val = mlir::narval::ConstantOp::create(b, loc, vt, name_attr).getResult();

    ctx.push_value(nir_call_runtime(ctx, loc, "nv_instanceof", {obj, name_val}, {vt}));
}
