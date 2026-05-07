#include "frontend/ast/expressions/slice_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

// Sentinela: INT32_MIN == "não especificado" (mesmo valor definido no runtime)
static constexpr int32_t NV_SLICE_NONE = -2147483648;

void SliceExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    auto& b = ctx.get_builder();
    auto& c = ctx.get_context();
    auto* I32    = llvm::Type::getInt32Ty(c);
    auto* VoidTy = llvm::Type::getVoidTy(c);

    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

    // Gerar collection (base)
    if (collection) collection->codegen(ctx);
    llvm::Value* base_val = ctx.pop_value();

    // Helper: gera um componente ou retorna a constante sentinela
    auto gen_component = [&](std::unique_ptr<Expr>& comp) -> llvm::Value* {
        if (!comp) return llvm::ConstantInt::get(I32, NV_SLICE_NONE);
        comp->codegen(ctx);
        llvm::Value* v = ctx.pop_value();
        if (!v) return llvm::ConstantInt::get(I32, NV_SLICE_NONE);
        // Se veio como Value struct (variável), extrai inteiro via runtime
        if (v->getType() == ValueTy) {
            auto decl = ctx.get_module().getOrInsertFunction(
                "extract_int_from_value",
                llvm::FunctionType::get(I32, {ValuePtr}, false)
            );
            auto* alloc = ctx.create_alloca(ValueTy, "slice.comp");
            b.CreateStore(v, alloc);
            return b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {alloc});
        }
        return nv::ir_utils::promote_type(ctx, v, I32);
    };

    llvm::Value* v_start = gen_component(start);
    llvm::Value* v_stop  = gen_component(stop);
    llvm::Value* v_step  = gen_component(step);

    // Chamar nv_collection_slice(out, self, start, stop, step)
    auto decl_slice = ctx.get_module().getOrInsertFunction(
        "nv_collection_slice",
        llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, I32, I32, I32}, false)
    );

    auto* out  = ctx.create_alloca(ValueTy, "slice.out");
    auto* self = ctx.create_alloca(ValueTy, "slice.self");

    if (base_val && base_val->getType() == ValueTy)
        b.CreateStore(base_val, self);

    b.CreateCall(llvm::cast<llvm::Function>(decl_slice.getCallee()),
                 {out, self, v_start, v_stop, v_step});

    ctx.push_value(b.CreateLoad(ValueTy, out));
}
