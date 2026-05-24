#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/char_literal_node.hpp"

void CharLiteralNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    // Box char as an i64 integer constant — same representation as int in NIR.
    ctx.push_value(nir_emit_const(ctx, loc,
        ctx.get_builder().getI64IntegerAttr((int64_t)(unsigned char)value)));
}
