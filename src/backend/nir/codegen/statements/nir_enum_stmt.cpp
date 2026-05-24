#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/enum_stmt_node.hpp"

void EnumStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc      = ctx.loc(position.get());
    auto  vt       = ctx.get_narval_value_type();
    auto& b        = ctx.get_builder();
    auto& mlir_ctx = ctx.get_mlir_context();

    // Create the enum object as a map.
    mlir::Value enum_map = nir_call_runtime(ctx, loc, "nv_create_map", {}, {vt});

    // Set each variant as an integer field via SetFieldOp (field_name is a StringAttr).
    int next_val = 0;
    for (const auto& variant : variants) {
        int val = variant.has_explicit_value ? variant.explicit_value : next_val;
        next_val = val + 1;

        auto int_val = nir_emit_const(ctx, loc, b.getI64IntegerAttr(val));
        mlir::narval::SetFieldOp::create(b, loc, enum_map,
            mlir::StringAttr::get(&mlir_ctx, variant.name), int_val);
    }

    ctx.define(name, enum_map);
}
