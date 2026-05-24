#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/throw_stmt_node.hpp"

void ThrowStatementNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());

    mlir::Value exc;
    if (exception) {
        exception->nir_codegen(ctx);
        exc = ctx.has_value() ? ctx.pop_value() : mlir::Value{};
    }
    if (!exc) exc = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));

    nir_call_runtime(ctx, loc, "nv_throw", {exc}, {});
}
