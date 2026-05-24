#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/slice_expr_node.hpp"

// INT32_MIN is the sentinel value meaning "not specified" in nv_collection_slice.
static constexpr int32_t NV_SLICE_NONE = -2147483648;

void SliceExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();
    auto& b  = ctx.get_builder();

    auto none_val = [&]() -> mlir::Value {
        return nir_emit_const(ctx, loc, b.getI64IntegerAttr(NV_SLICE_NONE));
    };

    if (collection) collection->nir_codegen(ctx);
    mlir::Value base = ctx.has_value() ? ctx.pop_value() : none_val();
    if (!base) base = none_val();

    mlir::Value v_start, v_stop, v_step;
    if (start) { start->nir_codegen(ctx); v_start = ctx.has_value() ? ctx.pop_value() : none_val(); }
    else v_start = none_val();
    if (stop)  { stop->nir_codegen(ctx);  v_stop  = ctx.has_value() ? ctx.pop_value() : none_val(); }
    else v_stop = none_val();
    if (step)  { step->nir_codegen(ctx);  v_step  = ctx.has_value() ? ctx.pop_value() : none_val(); }
    else v_step = none_val();

    ctx.push_value(nir_call_runtime(ctx, loc, "nv_slice",
        {base, v_start, v_stop, v_step}, {vt}));
}
