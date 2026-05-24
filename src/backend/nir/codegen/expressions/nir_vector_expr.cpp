#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/vector_expr_node.hpp"

void VectorExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();
    auto& b  = ctx.get_builder();

    unsigned N = static_cast<unsigned>(elements.size());
    auto sz  = nir_emit_const(ctx, loc, b.getI64IntegerAttr(N));

    // create_vector(capacity) -> Value
    mlir::Value vec = nir_call_runtime(ctx, loc, "nv_create_vector", {sz}, {vt});

    for (unsigned i = 0; i < N; ++i) {
        mlir::Value elt;
        if (elements[i]) {
            elements[i]->nir_codegen(ctx);
            elt = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
        }
        if (!elt) elt = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
        nir_call_runtime(ctx, loc, "nv_vector_push", {vec, elt}, {});
    }

    ctx.push_value(vec);
}
