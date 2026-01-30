#include "frontend/ast/expressions/array_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/checker.hpp"
#include <llvm/IR/DerivedTypes.h>

void ArrayExprNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    auto& c = ctx.get_context();

    unsigned N = static_cast<unsigned>(elements.size());

    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    auto* outArr = ctx.create_alloca(ValueTy, "arr.val");

    // Determinar se é Array ou Vector baseado no tipo inferido pelo checker
    // ArrayExprNode pode gerar arrays (quando tipos homogêneos) ou vectors (quando heterogêneos)
    bool is_vector = false;
    size_t declared_array_size = 0;
    if (ctx.get_type_checker()) {
        auto* checker = static_cast<rph::Checker*>(ctx.get_type_checker());
        try {
            auto inferred_type = checker->infer_expr(this);
            inferred_type = ctx.resolve_type(inferred_type);
            if (inferred_type && inferred_type->kind == rph::Kind::VECTOR) {
                is_vector = true;
            } else if (inferred_type && inferred_type->kind == rph::Kind::ARRAY) {
                // Verificar tamanho do array declarado
                auto* arr_type = static_cast<rph::Array*>(inferred_type.get());
                declared_array_size = arr_type->size;
                
                // Verificar se o número de elementos não excede o tamanho declarado
                if (N > declared_array_size) {
                    throw std::runtime_error("Array size mismatch: declared size is " + 
                                           std::to_string(declared_array_size) + 
                                           ", but " + std::to_string(N) + 
                                           " elements were provided.");
                }
            }
        } catch (const std::runtime_error& e) {
            // Re-lançar erros de tamanho
            throw;
        } catch (...) {
            // Se não conseguir inferir, assumir Array (comportamento padrão)
        }
    }

    if (is_vector) {
        // Vector: usar create_vector e vector_push_method
        auto decl_create_vector = ctx.get_module().getOrInsertFunction(
            "create_vector",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c)}, false)
        );
        b.CreateCall(llvm::cast<llvm::Function>(decl_create_vector.getCallee()), {outArr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), N)});
    } else {
        // Array: usar create_array e array_set_index_v
        auto decl_create_array = ctx.get_module().getOrInsertFunction(
            "create_array",
            llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c)}, false)
        );
        b.CreateCall(llvm::cast<llvm::Function>(decl_create_array.getCallee()), {outArr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), N)});
    }

    auto box_arg = [&](llvm::Value* any) -> llvm::Value* {
        auto* tmp = ctx.create_alloca(ValueTy, "elt");
        if (any->getType() == ValueTy) {
            b.CreateStore(any, tmp);
        } else if (any->getType()->isIntegerTy(1)) {
            auto decl = ctx.get_module().getOrInsertFunction("create_bool", llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c)}, false));
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, b.CreateZExt(any, llvm::Type::getInt32Ty(c))});
        } else if (any->getType()->isIntegerTy()) {
            auto decl = ctx.get_module().getOrInsertFunction("create_int", llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c)}, false));
            auto* I32 = llvm::Type::getInt32Ty(c);
            llvm::Value* iv = any->getType()->isIntegerTy(32) ? any : b.CreateSExtOrTrunc(any, I32);
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, iv});
        } else if (any->getType()->isFloatingPointTy()) {
            auto* F64 = llvm::Type::getDoubleTy(c);
            llvm::Value* fp = any;
            if (any->getType() != F64) fp = b.CreateFPExt(any, F64);
            auto decl = ctx.get_module().getOrInsertFunction("create_float", llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, F64}, false));
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, fp});
        } else if (any->getType() == rph::ir_utils::get_i8_ptr(ctx)) {
            auto decl = ctx.get_module().getOrInsertFunction("create_str", llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, rph::ir_utils::get_i8_ptr(ctx)}, false));
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, any});
        } else {
            b.CreateStore(llvm::UndefValue::get(ValueTy), tmp);
        }
        return tmp;
    };

    for (unsigned i = 0; i < N; ++i) {
        llvm::Value* ev = nullptr;
        if (elements[i]) { elements[i]->codegen(ctx); if (ctx.has_value()) ev = ctx.pop_value(); }
        if (!ev) ev = llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), 0);
        auto* boxed = box_arg(ev);
        
        if (is_vector) {
            // Vector: usar vector_push_method(out, self, value)
            auto decl_push = ctx.get_module().getOrInsertFunction(
                "vector_push_method",
                llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, ValuePtr, ValuePtr}, false)
            );
            auto* tmp_out = ctx.create_alloca(ValueTy, "tmp.out");
            b.CreateCall(llvm::cast<llvm::Function>(decl_push.getCallee()), {tmp_out, outArr, boxed});
        } else {
            // Array: usar array_set_index_v(self, index, value)
            auto decl_set = ctx.get_module().getOrInsertFunction(
                "array_set_index_v",
                llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c), ValuePtr}, false)
            );
            b.CreateCall(llvm::cast<llvm::Function>(decl_set.getCallee()), {outArr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), i), boxed});
        }
    }

    // Return the array/vector Value aggregate
    ctx.push_value(b.CreateLoad(ValueTy, outArr));
}
