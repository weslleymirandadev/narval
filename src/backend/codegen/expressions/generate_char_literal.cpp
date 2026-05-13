#include "frontend/ast/expressions/char_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"

void CharLiteralNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    auto& B = ctx.get_builder();
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I8 = llvm::Type::getInt8Ty(ctx.get_context());

    auto* out = ctx.create_alloca(ValueTy, "char.literal");
    auto* create_char = ctx.ensure_runtime_func("create_char", {ValuePtr, I8});
    B.CreateCall(create_char, {out, nv::ir_utils::create_char_constant(ctx, value)});
    ctx.push_value(B.CreateLoad(ValueTy, out, "char.val"));
}
