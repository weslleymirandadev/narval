#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/string_literal_node.hpp"

void StringLiteralNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc  = ctx.loc(position.get());
    auto attr = ctx.get_builder().getStringAttr(value);
    ctx.push_value(nir_emit_const(ctx, loc, attr));
}
