#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"

IdentifierNode::~IdentifierNode() = default;

void IdentifierNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    mlir::Value v = ctx.lookup(symbol);
    if (!v)
        v = nir_emit_const(ctx, ctx.loc(position.get()),
                           ctx.get_builder().getI64IntegerAttr(0));
    ctx.push_value(v);
}
