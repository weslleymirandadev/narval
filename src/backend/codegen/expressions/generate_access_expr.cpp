#include "frontend/ast/expressions/access_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void AccessExprNode::codegen(rph::IRGenerationContext& ctx) {
    // base[ index ]
    if (expr) expr->codegen(ctx);
    llvm::Value* base = ctx.pop_value();
    if (index) index->codegen(ctx);
    llvm::Value* idx_v = ctx.pop_value();

    auto& b = ctx.get_builder();
    auto& c = ctx.get_context();

    // If base is a runtime Value aggregate, use array_get_index_v
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    if (base && base->getType() == ValueTy) {
        // ensure index is i32
        auto* I32 = llvm::Type::getInt32Ty(c);
        if (idx_v && idx_v->getType() != I32) idx_v = rph::ir_utils::promote_type(ctx, idx_v, I32);
        if (!idx_v) idx_v = llvm::ConstantInt::get(I32, 0);

        auto decl_get = ctx.get_module().getOrInsertFunction(
            "array_get_index_v",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, ValuePtr, I32}, false)
        );
        // out, self, index
        auto* out = ctx.create_alloca(ValueTy, "idx.out");
        auto* self = ctx.create_alloca(ValueTy, "idx.self");
        b.CreateStore(base, self);
        b.CreateCall(llvm::cast<llvm::Function>(decl_get.getCallee()), {out, self, idx_v});
        ctx.push_value(b.CreateLoad(ValueTy, out));
        return;
    }

    // Fallback: unknown base kind
    ctx.push_value(nullptr);
}
