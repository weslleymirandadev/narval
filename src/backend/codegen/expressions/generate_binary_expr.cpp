#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "frontend/checker/type.hpp"
#include "backend/runtime/prototypes.h"
#ifdef NARVAL_USE_NIR
#  include "backend/nir/tensor_nir.hpp"
#endif

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

    //  Tensor operations 
    // The type-checker annotated tensor_op if either operand is a tensor.
    if (!tensor_op.empty()) {
        if (left)  left->codegen(ctx);
        auto* lhs_v2 = ctx.pop_value();
        if (right) right->codegen(ctx);
        auto* rhs_v2 = ctx.pop_value();
        if (!lhs_v2 || !rhs_v2) { ctx.push_value(nullptr); return; }

        auto& B       = ctx.get_builder();
        auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
        auto* F64      = llvm::Type::getDoubleTy(ctx.get_context());

        // Helper: store a Value struct onto the stack and get its address.
        auto box = [&](llvm::Value* v, const char* name) -> llvm::Value* {
            auto* a = ctx.create_alloca(ValueTy, name);
            if (v->getType() == ValueTy)         B.CreateStore(v, a);
            else if (v->getType()->isPointerTy()) B.CreateStore(B.CreateLoad(ValueTy, v), a);
            return a;
        };

        if (tensor_op == "matmul") {
            llvm::Value* result_v = nullptr;
#ifdef NARVAL_USE_NIR
            result_v = nv::emit_tensor_matmul_nir(ctx, lhs_v2, rhs_v2,
                                                   lhs_tensor_dims, rhs_tensor_dims);
#endif
            if (!result_v) {
                auto* fn = ctx.ensure_runtime_func("nv_tensor_matmul",
                               {ValuePtr, ValuePtr}, ValueTy);
                result_v = B.CreateCall(fn, {box(lhs_v2, "tm_a"), box(rhs_v2, "tm_b")}, "tmat");
            }
            ctx.push_value(result_v);
            return;
        }

        if (tensor_op == "add" || tensor_op == "sub" || tensor_op == "mul") {
            llvm::Value* result_v = nullptr;
#ifdef NARVAL_USE_NIR
            result_v = nv::emit_tensor_elemwise_nir(ctx, lhs_v2, rhs_v2,
                                                     lhs_tensor_dims, tensor_op);
#endif
            if (!result_v) {
                const char* rt_fn = (tensor_op == "sub") ? "nv_tensor_sub"
                                  : (tensor_op == "mul") ? "nv_tensor_mul"
                                  :                        "nv_tensor_add";
                auto* fn = ctx.ensure_runtime_func(rt_fn, {ValuePtr, ValuePtr}, ValueTy);
                result_v = B.CreateCall(fn, {box(lhs_v2, "ta_a"), box(rhs_v2, "ta_b")}, "tew");
            }
            ctx.push_value(result_v);
            return;
        }

        if (tensor_op == "scalar_mul") {
            auto* fn = ctx.ensure_runtime_func("nv_tensor_scalar_mul",
                           {ValuePtr, F64}, ValueTy);
            llvm::Value* tensor_a = lhs_v2, *scalar = rhs_v2;
            if (rhs_v2->getType() == ValueTy) { tensor_a = rhs_v2; scalar = lhs_v2; }
            auto* sf = scalar->getType()->isDoubleTy() ? scalar
                     : B.CreateSIToFP(scalar, F64, "smul_f");
            ctx.push_value(B.CreateCall(fn, {box(tensor_a, "tsm_a"), sf}, "tsmul"));
            return;
        }

        // Fallback for any other annotated tensor op.
        {
            auto* fn = ctx.ensure_runtime_func("nv_tensor_add",
                           {ValuePtr, ValuePtr}, ValueTy);
            ctx.push_value(B.CreateCall(fn, {box(lhs_v2, "tf_a"), box(rhs_v2, "tf_b")}, "tfb"));
        }
        return;
    }

    if (left) left->codegen(ctx);
    auto* lhs_v = ctx.pop_value();
    if (right) right->codegen(ctx);
    auto* rhs_v = ctx.pop_value();
    if (!lhs_v || !rhs_v) { ctx.push_value(nullptr); return; }



    // Helper: coerce any value to i1 for logical ops
    auto to_i1 = [&](llvm::Value* v) -> llvm::Value* {
        if (!v) return ctx.get_builder().getFalse();
        auto& b = ctx.get_builder();
        auto* I32     = llvm::Type::getInt32Ty(ctx.get_context());
        auto* I1      = llvm::Type::getInt1Ty(ctx.get_context());
        auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

        if (v->getType() == I1) return v;   // already i1

        // ValueTy struct: use nv_bool_convert then nv_value_cmp with zero
        if (v->getType() == ValueTy) {
            auto* in  = ctx.create_alloca(ValueTy, "li_in");
            auto* out = ctx.create_alloca(ValueTy, "li_out");
            b.CreateStore(v, in);
            auto* conv = ctx.ensure_runtime_func("nv_bool_convert", {ValuePtr, ValuePtr});
            b.CreateCall(conv, {out, in});
            // nv_bool_convert writes a bool Value; extract its int representation
            auto* zero_val = ctx.create_alloca(ValueTy, "li_zero");
            auto* ci_fn    = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
            b.CreateCall(ci_fn, {zero_val, llvm::ConstantInt::get(I32, 0)});
            auto* cmp_fn   = ctx.ensure_runtime_func("nv_value_cmp", {ValuePtr, ValuePtr}, I32);
            auto* cmp_i32  = b.CreateCall(cmp_fn, {out, zero_val}, "li_cmp");
            // cmp_i32 is i32; zero is i32 — safe ICmpNE
            return b.CreateICmpNE(cmp_i32, llvm::ConstantInt::get(I32, 0), "li_i1");
        }

        // Any other integer: zero-extend or truncate to i1
        if (v->getType()->isIntegerTy()) {
            auto* i32v = v->getType()->isIntegerTy(32)
                ? v : b.CreateSExtOrTrunc(v, I32, "li_i32");
            return b.CreateICmpNE(i32v, llvm::ConstantInt::get(I32, 0), "li_i1");
        }

        return b.CreateIsNotNull(v, "li_notnull");
    };

    // Logical AND / OR
    if (op == "&&") {
        // CORREÇÃO: Forçar conversão explícita para i1 se necessário
        auto* lhs_i1 = to_i1(lhs_v);
        auto* rhs_i1 = to_i1(rhs_v);
        ctx.push_value(ctx.get_builder().CreateAnd(lhs_i1, rhs_i1, "land"));
        return;
    }
    if (op == "||") {
        ctx.push_value(ctx.get_builder().CreateOr(to_i1(lhs_v), to_i1(rhs_v), "lor"));
        return;
    }

    // Comparisons
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
                
        auto* cmp = nv::ir_utils::create_comparison(ctx, lhs_v, rhs_v, op);
        ctx.push_value(cmp);
        return;
    }

    // Arithmetic
    ctx.push_value(nv::ir_utils::create_binary_op(ctx, lhs_v, rhs_v, op));
}
