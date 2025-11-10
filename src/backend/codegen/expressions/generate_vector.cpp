#include "frontend/ast/expressions/vector_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/DerivedTypes.h>

void VectorExprNode::codegen(rph::IRGenerationContext& ctx) {
    auto& b = ctx.get_builder();
    auto& c = ctx.get_context();

    unsigned N = static_cast<unsigned>(elements.size());

    // Create runtime vector Value via create_vector(out, capacity)
    auto* ValueTy = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);
    auto* outVec = ctx.create_alloca(ValueTy, "vec.val");

    auto decl_create_vector = ctx.get_module().getOrInsertFunction(
        "create_vector",
        llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c)}, false)
    );
    b.CreateCall(llvm::cast<llvm::Function>(decl_create_vector.getCallee()), {outVec, llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), N)});

    // Prepare decl for vector_push_method(out, self, Value*)
    auto decl_push = ctx.get_module().getOrInsertFunction(
        "vector_push_method",
        llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, ValuePtr, ValuePtr}, false)
    );

    auto box_arg = [&](llvm::Value* any) -> llvm::Value* {
        auto* tmp = ctx.create_alloca(ValueTy, "elt");
        if (any->getType() == ValueTy) {
            b.CreateStore(any, tmp);
        } else if (any->getType()->isStructTy()) {
            auto* st = llvm::cast<llvm::StructType>(any->getType());
            if (st->hasName() && st->getName().starts_with("rph.tuple.")) {
                // Create runtime tuple Value and fill its fields
                unsigned N = st->getNumElements();
                auto decl_ct = ctx.get_module().getOrInsertFunction(
                    "create_tuple",
                    llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c)}, false)
                );
                b.CreateCall(llvm::cast<llvm::Function>(decl_ct.getCallee()), {tmp, llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), N)});

                // Alloca the tuple value to access fields
                auto* tupAlloca = ctx.create_alloca(st, "tuple.val");
                b.CreateStore(any, tupAlloca);

                // Declare tuple_set_impl(Value* self, int index, const Value* v)
                auto* f_ty = llvm::FunctionType::get(
                    llvm::Type::getVoidTy(c), { ValuePtr, llvm::Type::getInt32Ty(c), ValuePtr }, false);
                auto decl_set = ctx.get_module().getOrInsertFunction("tuple_set_impl", f_ty);

                for (unsigned i = 0; i < N; ++i) {
                    auto* fldPtr = b.CreateStructGEP(st, tupAlloca, i);
                    auto* fldTy  = st->getElementType(i);
                    llvm::Value* fldVal = b.CreateLoad(fldTy, fldPtr);
                    // Box field into a Value
                    auto* boxedFld = ctx.create_alloca(ValueTy, "tup.elt");
                    if (fldTy->isIntegerTy(1)) {
                        auto decl = ctx.get_module().getOrInsertFunction("create_bool", llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c)}, false));
                        b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {boxedFld, b.CreateZExt(fldVal, llvm::Type::getInt32Ty(c))});
                    } else if (fldTy->isIntegerTy()) {
                        auto decl = ctx.get_module().getOrInsertFunction("create_int", llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, llvm::Type::getInt32Ty(c)}, false));
                        auto* I32 = llvm::Type::getInt32Ty(c);
                        llvm::Value* iv = fldTy->isIntegerTy(32) ? fldVal : b.CreateSExtOrTrunc(fldVal, I32);
                        b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {boxedFld, iv});
                    } else if (fldTy->isFloatingPointTy()) {
                        auto* F64 = llvm::Type::getDoubleTy(c);
                        llvm::Value* fp = fldVal;
                        if (fldTy != F64) fp = b.CreateFPExt(fldVal, F64);
                        auto decl = ctx.get_module().getOrInsertFunction("create_float", llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, F64}, false));
                        b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {boxedFld, fp});
                    } else if (fldTy->isPointerTy()) {
                        auto i8p = rph::ir_utils::get_i8_ptr(ctx);
                        auto decl = ctx.get_module().getOrInsertFunction("create_str", llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, i8p}, false));
                        auto* casted = b.CreateBitCast(fldVal, i8p);
                        b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {boxedFld, casted});
                    } else {
                        // Fallback: store undef
                        b.CreateStore(llvm::UndefValue::get(ValueTy), boxedFld);
                    }
                    // Pass pointer to boxed value to tuple_set_impl
                    b.CreateCall(llvm::cast<llvm::Function>(decl_set.getCallee()), { tmp, llvm::ConstantInt::get(llvm::Type::getInt32Ty(c), (int)i), boxedFld });
                }
            } else {
                b.CreateStore(llvm::UndefValue::get(ValueTy), tmp);
            }
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
        } else if (any->getType()->isPointerTy()) {
            auto i8p = rph::ir_utils::get_i8_ptr(ctx);
            auto decl = ctx.get_module().getOrInsertFunction("create_str", llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, i8p}, false));
            auto* casted = b.CreateBitCast(any, i8p);
            b.CreateCall(llvm::cast<llvm::Function>(decl.getCallee()), {tmp, casted});
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
        // push into vector: vector_push_method(out_tmp, self_vec, boxed)
        auto* tmp_out = ctx.create_alloca(ValueTy, "tmp.out");
        if (auto* F = llvm::dyn_cast<llvm::Function>(decl_push.getCallee())) {
            // attributes for sret already added on param 0
        }
        b.CreateCall(llvm::cast<llvm::Function>(decl_push.getCallee()), {tmp_out, outVec, boxed});
    }

    // Return the vector Value aggregate
    ctx.push_value(b.CreateLoad(ValueTy, outVec));
}
