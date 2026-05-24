#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/array_expr_node.hpp"

void ArrayExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();
    auto& b  = ctx.get_builder();

    unsigned N = static_cast<unsigned>(elements.size());
    auto sz  = nir_emit_const(ctx, loc, b.getI64IntegerAttr(N));

    // create_array(size) -> Value
    mlir::Value arr = nir_call_runtime(ctx, loc, "nv_create_array", {sz}, {vt});

    for (unsigned i = 0; i < N; ++i) {
        mlir::Value elt;
        if (elements[i]) {
            elements[i]->nir_codegen(ctx);
            elt = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
        }
        if (!elt) elt = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
        auto idx = nir_emit_const(ctx, loc, b.getI64IntegerAttr(i));
        nir_call_runtime(ctx, loc, "nv_array_set", {arr, idx, elt}, {});
    }

    ctx.push_value(arr);
}
