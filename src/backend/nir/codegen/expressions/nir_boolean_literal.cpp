#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/boolean_literal_node.hpp"

void BooleanLiteralNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc  = ctx.loc(position.get());
    auto attr = ctx.get_builder().getBoolAttr(value);
    ctx.push_value(nir_emit_const(ctx, loc, attr));
}
