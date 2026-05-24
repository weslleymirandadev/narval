#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/self_expr_node.hpp"

void SelfExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    mlir::Value self = ctx.lookup("__this");
    if (!self)
        self = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));
    ctx.push_value(self);
}
