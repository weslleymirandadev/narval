#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

// Box a raw LLVM value into a Value* alloca (mesmo padrão de generate_call_expr.cpp)
static llvm::Value* box_for_op(nv::IRGenerationContext& ctx, llvm::Value* v) {
    auto& B = ctx.get_builder();
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* alloca   = ctx.create_alloca(ValueTy, "op_box");
    if (!v) { B.CreateStore(llvm::UndefValue::get(ValueTy), alloca); return alloca; }
    if (v->getType() == ValueTy) { B.CreateStore(v, alloca); return alloca; }
    auto* I32 = llvm::Type::getInt32Ty(ctx.get_context());
    auto* F64 = llvm::Type::getDoubleTy(ctx.get_context());
    if (v->getType()->isIntegerTy(1)) {
        auto* f = ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
        B.CreateCall(f, {alloca, B.CreateZExt(v, I32)});
    } else if (v->getType()->isIntegerTy()) {
        auto* f = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
        llvm::Value* iv = v->getType()->isIntegerTy(32) ? v : B.CreateSExtOrTrunc(v, I32);
        B.CreateCall(f, {alloca, iv});
    } else if (v->getType()->isFloatingPointTy()) {
        auto* f = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
        llvm::Value* fv = (v->getType() == F64) ? v : B.CreateFPExt(v, F64);
        B.CreateCall(f, {alloca, fv});
    } else if (v->getType()->isPointerTy()) {
        auto* I8P = nv::ir_utils::get_i8_ptr(ctx);
        llvm::Value* s = (v->getType() == I8P) ? v : B.CreateBitCast(v, I8P);
        auto* f = ctx.ensure_runtime_func("create_str", {ValuePtr, I8P});
        B.CreateCall(f, {alloca, s});
    } else {
        B.CreateStore(llvm::UndefValue::get(ValueTy), alloca);
    }
    return alloca;
}

void BinaryExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    // Operator overloading: o type-checker anotou overload_class + overload_dunder
    // durante a fase de verificação de tipos. Usamos diretamente aqui sem re-invocar o checker.
    if (!overload_class.empty() && !overload_dunder.empty()) {
        std::string fn_name = "__method_" + overload_class + "_" + overload_dunder;
        auto* method_fn = ctx.get_module().getFunction(fn_name);
        if (method_fn) {
            auto& B       = ctx.get_builder();
            auto* ValueTy = nv::ir_utils::get_value_struct(ctx);

            if (left)  left->codegen(ctx);
            auto* lhs_v = ctx.pop_value();
            if (right) right->codegen(ctx);
            auto* rhs_v = ctx.pop_value();

            // Self como Value*
            auto* self_alloca = ctx.create_alloca(ValueTy, "op_self");
            if (lhs_v && lhs_v->getType() == ValueTy)
                B.CreateStore(lhs_v, self_alloca);
            else if (lhs_v && lhs_v->getType()->isPointerTy())
                B.CreateStore(B.CreateLoad(ValueTy, lhs_v), self_alloca);
            else
                B.CreateStore(llvm::UndefValue::get(ValueTy), self_alloca);

            auto* rhs_boxed = box_for_op(ctx, rhs_v);
            auto* call = B.CreateCall(method_fn, {self_alloca, rhs_boxed});
            ctx.push_value(method_fn->getReturnType()->isVoidTy()
                ? llvm::UndefValue::get(ValueTy)
                : static_cast<llvm::Value*>(call));
            return;
        }
    }

    if (left) left->codegen(ctx);
    auto* lhs_v = ctx.pop_value();
    if (right) right->codegen(ctx);
    auto* rhs_v = ctx.pop_value();
    if (!lhs_v || !rhs_v) { ctx.push_value(nullptr); return; }

    // Logical AND / OR (assume i1 operands; if not, compare != 0)
    if (op == "&&") {
        auto& b = ctx.get_builder();
        if (!lhs_v->getType()->isIntegerTy(1)) lhs_v = b.CreateICmpNE(lhs_v, llvm::ConstantInt::get(lhs_v->getType(), 0));
        if (!rhs_v->getType()->isIntegerTy(1)) rhs_v = b.CreateICmpNE(rhs_v, llvm::ConstantInt::get(rhs_v->getType(), 0));
        ctx.push_value(b.CreateAnd(lhs_v, rhs_v, "land"));
        return;
    }
    if (op == "||") {
        auto& b = ctx.get_builder();
        if (!lhs_v->getType()->isIntegerTy(1)) lhs_v = b.CreateICmpNE(lhs_v, llvm::ConstantInt::get(lhs_v->getType(), 0));
        if (!rhs_v->getType()->isIntegerTy(1)) rhs_v = b.CreateICmpNE(rhs_v, llvm::ConstantInt::get(rhs_v->getType(), 0));
        ctx.push_value(b.CreateOr(lhs_v, rhs_v, "lor"));
        return;
    }

    // Comparisons
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
        ctx.push_value(nv::ir_utils::create_comparison(ctx, lhs_v, rhs_v, op));
        return;
    }

    // Arithmetic
    ctx.push_value(nv::ir_utils::create_binary_op(ctx, lhs_v, rhs_v, op));
}
