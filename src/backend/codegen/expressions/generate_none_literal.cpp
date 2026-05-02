#include "frontend/ast/expressions/none_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void NoneLiteralNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& B       = ctx.get_builder();
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

    auto* out = ctx.create_alloca(ValueTy, "none_val");
    auto* fn  = ctx.ensure_runtime_func("create_option_none", {ValuePtr});
    B.CreateCall(fn, {out});
    ctx.push_value(B.CreateLoad(ValueTy, out, "none"));
}
