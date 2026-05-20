#include "frontend/ast/statements/defer_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

using namespace llvm;

// Emite o defer_body no bloco atual (sem criar escopo extra — chamado nos dois caminhos).
static void emit_defer_body(DeferStmtNode* defer, nv::IRGenerationContext& ctx) {
    auto& B = ctx.get_builder();
    ctx.enter_scope();
    for (auto& stmt : defer->defer_body) {
        if (stmt) {
            stmt->codegen(ctx);
            if (B.GetInsertBlock()->getTerminator()) break;
        }
    }
    ctx.exit_scope();
}

// `defer { body }` — equivalente a:
//   try { remaining_body } finally { body }
// Usando setjmp/longjmp: no caminho normal executa body após remaining_body;
// no caminho de exceção executa body e re-lança.
void DeferStmtNode::codegen(nv::IRGenerationContext& ctx) {
    auto& B = ctx.get_builder();
    auto& M = ctx.get_module();
    auto& C = ctx.get_context();

    if (nv::get_feature_tracker().no_std) {
        // Sem runtime: apenas emite remaining_body seguido de defer_body (sem proteção contra exceções).
        ctx.enter_scope();
        for (auto& stmt : remaining_body) {
            if (stmt) {
                stmt->codegen(ctx);
                if (B.GetInsertBlock()->getTerminator()) break;
            }
        }
        ctx.exit_scope();
        emit_defer_body(this, ctx);
        return;
    }

    auto* I8Ptr  = PointerType::getUnqual(C);
    auto* I32    = Type::getInt32Ty(C);

    auto* push_fn    = ctx.ensure_runtime_func("nv_push_try_handler",          {}, I8Ptr);
    auto* pop_fn     = ctx.ensure_runtime_func("nv_pop_try_handler",           {});
    auto* rethrow_fn = ctx.ensure_runtime_func("nv_rethrow_current_exception", {});

    auto setjmp_callee = M.getOrInsertFunction(
        "setjmp", FunctionType::get(I32, {I8Ptr}, false));
    auto* setjmp_fn = cast<Function>(setjmp_callee.getCallee());
    setjmp_fn->addFnAttr(Attribute::ReturnsTwice);

    // 1. Push handler + setjmp
    auto* handler_ptr   = B.CreateCall(push_fn, {}, "defer_handler_ptr");
    auto* setjmp_result = B.CreateCall(setjmp_fn, {handler_ptr}, "defer_setjmp");
    auto* is_exception  = B.CreateICmpNE(setjmp_result, ConstantInt::get(I32, 0), "defer.caught");

    auto* body_bb    = ctx.create_block("defer.body");
    auto* exc_bb     = ctx.create_block("defer.exc");
    auto* cleanup_bb = ctx.create_block("defer.cleanup");
    auto* merge_bb   = ctx.create_block("defer.merge");

    B.CreateCondBr(is_exception, exc_bb, body_bb);

    // 2. Caminho normal: remaining_body → pop → cleanup
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
        B.CreateBr(cleanup_bb);
    }

    // 3. Caminho de exceção: defer_body → re-throw
    B.SetInsertPoint(exc_bb);
    emit_defer_body(this, ctx);
    if (!B.GetInsertBlock()->getTerminator()) {
        B.CreateCall(rethrow_fn, {});
        B.CreateUnreachable();
    }

    // 4. Caminho normal pós-body: defer_body → merge
    B.SetInsertPoint(cleanup_bb);
    emit_defer_body(this, ctx);
    if (!B.GetInsertBlock()->getTerminator())
        B.CreateBr(merge_bb);

    // 5. Merge
    B.SetInsertPoint(merge_bb);
}
