#include "frontend/ast/expressions/await_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void AwaitExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    if (operand) {
        operand->codegen(ctx);
    }

    auto* future_val = ctx.has_value() ? ctx.pop_value() : nullptr;
    auto& B = ctx.get_builder();
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

    auto* future_slot = ctx.create_alloca(ValueTy, "await.future");
    if (future_val && future_val->getType() == ValueTy) {
        B.CreateStore(future_val, future_slot);
    } else if (future_val) {
        auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
        auto* F64 = llvm::Type::getDoubleTy(ctx.get_context());
        auto* I8P = nv::ir_utils::get_i8_ptr(ctx);
        if (future_val->getType()->isIntegerTy(1)) {
            auto* fn = ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
            B.CreateCall(fn, {future_slot, B.CreateZExt(future_val, I32)});
        } else if (future_val->getType()->isIntegerTy()) {
            auto* iv = future_val->getType()->isIntegerTy(32)
                ? future_val
                : B.CreateSExtOrTrunc(future_val, I32);
            auto* fn = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
            B.CreateCall(fn, {future_slot, iv});
        } else if (future_val->getType()->isFloatingPointTy()) {
            auto* fv = future_val->getType() == F64
                ? future_val
                : B.CreateFPExt(future_val, F64);
            auto* fn = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
            B.CreateCall(fn, {future_slot, fv});
        } else if (future_val->getType()->isPointerTy()) {
            auto* s = future_val->getType() == I8P
                ? future_val
                : B.CreateBitCast(future_val, I8P);
            auto* fn = ctx.ensure_runtime_func("create_str", {ValuePtr, I8P});
            B.CreateCall(fn, {future_slot, s});
        } else {
            B.CreateStore(llvm::UndefValue::get(ValueTy), future_slot);
        }
    } else {
        B.CreateStore(llvm::UndefValue::get(ValueTy), future_slot);
    }

    auto* out_slot = ctx.create_alloca(ValueTy, "await.result");
    auto* await_fn = ctx.ensure_runtime_func(
        "nv_await",
        {ValuePtr, ValuePtr},
        llvm::Type::getVoidTy(ctx.get_context()));
    B.CreateCall(await_fn, {out_slot, future_slot});
    ctx.push_value(B.CreateLoad(ValueTy, out_slot, "await.value"));
}
