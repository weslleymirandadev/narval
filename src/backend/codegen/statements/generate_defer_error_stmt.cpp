#include "frontend/ast/statements/defer_error_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

using namespace llvm;

// `defer error { handler }` é equivalente a:
//   try { remaining_body } catch Error err { handler }
// Usa o mesmo mecanismo setjmp/longjmp do try/catch existente.
void DeferErrorStmtNode::codegen(nv::IRGenerationContext& ctx) {
    auto& B   = ctx.get_builder();
    auto& M   = ctx.get_module();
    auto& C   = ctx.get_context();

    auto* I8Ptr   = PointerType::getUnqual(C);
    auto* I32     = Type::getInt32Ty(C);
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

    auto* push_fn    = ctx.ensure_runtime_func("nv_push_try_handler",       {}, I8Ptr);
    auto* pop_fn     = ctx.ensure_runtime_func("nv_pop_try_handler",        {});
    auto* get_err_fn = ctx.ensure_runtime_func("nv_get_current_exception_into", {ValuePtr});
    auto* clear_fn   = ctx.ensure_runtime_func("nv_clear_current_exception", {});

    auto setjmp_callee = M.getOrInsertFunction(
        "setjmp", FunctionType::get(I32, {I8Ptr}, false));
    auto* setjmp_fn = cast<Function>(setjmp_callee.getCallee());
    setjmp_fn->addFnAttr(Attribute::ReturnsTwice);

    // 1. Push handler + setjmp
    auto* handler_ptr   = B.CreateCall(push_fn, {}, "defer_handler_ptr");
    auto* setjmp_result = B.CreateCall(setjmp_fn, {handler_ptr}, "defer_setjmp");
    auto* is_exception  = B.CreateICmpNE(setjmp_result, ConstantInt::get(I32, 0), "defer.caught");

    auto* body_bb    = ctx.create_block("defer.body");
    auto* handler_bb = ctx.create_block("defer.handler");
    auto* merge_bb   = ctx.create_block("defer.merge");

    B.CreateCondBr(is_exception, handler_bb, body_bb);

    // 2. Corpo protegido (remaining_body)
    B.SetInsertPoint(body_bb);
    ctx.enter_scope();
    for (auto& stmt : remaining_body) {
        if (stmt) {
            stmt->codegen(ctx);
            if (B.GetInsertBlock()->getTerminator()) break;
        }
    }
    ctx.exit_scope();
    if (!B.GetInsertBlock()->getTerminator()) {
        B.CreateCall(pop_fn, {});
        B.CreateBr(merge_bb);
    }

    // 3. Handler de erro (catch Error err { handler_body })
    B.SetInsertPoint(handler_bb);
    ctx.enter_scope();

    auto* err_slot = ctx.create_alloca(ValueTy, "err");
    B.CreateCall(get_err_fn, {err_slot});
    nv::SymbolInfo err_info(err_slot, ValueTy, nullptr, true, false);
    ctx.get_symbol_table().define_symbol("err", err_info);
    B.CreateCall(clear_fn, {});

    for (auto& stmt : handler_body) {
        if (stmt) {
            stmt->codegen(ctx);
            if (B.GetInsertBlock()->getTerminator()) break;
        }
    }
    ctx.exit_scope();
    if (!B.GetInsertBlock()->getTerminator())
        B.CreateBr(merge_bb);

    // 4. Merge
    B.SetInsertPoint(merge_bb);
}
