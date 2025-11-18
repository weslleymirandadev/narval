#include "frontend/ast/expressions/array_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/DerivedTypes.h>

void ArrayExprNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    auto& c = ctx.get_context();

    unsigned N = static_cast<unsigned>(elements.size());

    // Create runtime array Value via create_array(out, N)
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    auto* outArr = ctx.create_alloca(ValueTy, "arr.val");

    auto decl_create_array = ctx.get_module().getOrInsertFunction(
        "create_array",
        llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c)}, false)
    );
    b.CreateCall(llvm::cast<llvm::Function>(decl_create_array.getCallee()), {outArr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), N)});

    // For each element, box into Value and set into array via array_set_index_v(outArr, index, Value*)
    auto decl_set = ctx.get_module().getOrInsertFunction(
        "array_set_index_v",
        llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c), ValuePtr}, false)
    );

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
        // Pass pointer to Value (boxed), not the aggregate by value
        b.CreateCall(llvm::cast<llvm::Function>(decl_set.getCallee()), {outArr, llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), i), boxed});
    }

    // Return the array Value aggregate
    ctx.push_value(b.CreateLoad(ValueTy, outArr));
}
