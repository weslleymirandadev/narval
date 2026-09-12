#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/tuple_expr_node.hpp"

void AccessExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();

    if (expr)  expr->nir_codegen(ctx);
    mlir::Value base = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
    if (index) index->nir_codegen(ctx);
    mlir::Value idx  = ctx.has_value() ? ctx.pop_value() : mlir::Value{};

    // A tensor addressed by several coordinates (marked by the checker): one row-major
    // offset from the tensor's own shape, then the ordinary flat access — the runtime
    // already handles a tensor there. The same logic is in nir_assignment_expr.cpp for
    // `t[i, j] = v`; keep the two in step.
    if (tensor_multi_index && index && index->kind == NodeType::TupleExpression) {
        auto* tup = static_cast<TupleExprNode*>(index.get());
        std::vector<mlir::Value> args;
        args.push_back(base);
        args.push_back(nir_emit_const(ctx, loc,
            ctx.get_builder().getI64IntegerAttr((int64_t)tup->elements.size())));
        for (auto& el : tup->elements) {
            el->nir_codegen(ctx);
            args.push_back(ctx.has_value() ? ctx.pop_value() : mlir::Value{});
        }
        while (args.size() < 6) {
            args.push_back(nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0)));
        }
        mlir::Value flat = nir_call_runtime(ctx, loc, "nv_tensor_flat_index", args, {vt});
        ctx.push_value(nir_call_runtime(ctx, loc, "nv_container_get", {base, flat}, {vt}));
        return;
    }

    if (!base) base = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));
    if (!idx)  idx  = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));

    // Dispatch on the receiver type at runtime: maps are keyed by string,
    // arrays/vectors/tuples by integer index.
    ctx.push_value(nir_call_runtime(ctx, loc, "nv_container_get", {base, idx}, {vt}));
}
