#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/none_literal_node.hpp"

void NoneLiteralNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    auto vt  = ctx.get_narval_value_type();
    // Emit a runtime call to get the singleton None value (NV_OPTION_NONE_BASE).
    ctx.push_value(nir_call_runtime(ctx, loc, "nv_make_none", {}, {vt}));
}
