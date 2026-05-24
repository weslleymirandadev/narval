#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/map_node.hpp"
#include "frontend/ast/expressions/key_value_node.hpp"
#include "frontend/ast/expressions/string_literal_node.hpp"

void MapNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc      = ctx.loc(position.get());
    auto  vt       = ctx.get_narval_value_type();
    auto& b        = ctx.get_builder();
    auto& mlir_ctx = ctx.get_mlir_context();

    mlir::Value map = nir_call_runtime(ctx, loc, "nv_create_map", {}, {vt});

    for (const auto& prop : properties) {
        if (!prop || prop->kind != NodeType::KeyValue) continue;
        auto* kv = static_cast<KeyValueNode*>(prop.get());

        if (kv->value) kv->value->nir_codegen(ctx);
        mlir::Value val = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
        if (!val) val = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));

        // If key is a string literal, use SetFieldOp directly.
        if (kv->key && kv->key->kind == NodeType::StringLiteral) {
            auto field = static_cast<StringLiteralNode*>(kv->key.get())->value;
            mlir::narval::SetFieldOp::create(b, loc, map,
                mlir::StringAttr::get(&mlir_ctx, field), val);
        } else {
            // Dynamic key: emit key as value and use runtime.
            if (kv->key) kv->key->nir_codegen(ctx);
            mlir::Value key_val = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
            if (!key_val) key_val = nir_emit_const(ctx, loc, b.getI64IntegerAttr(0));
            nir_call_runtime(ctx, loc, "nv_map_set_dynamic", {map, key_val, val}, {});
        }
    }

    ctx.push_value(map);
}
