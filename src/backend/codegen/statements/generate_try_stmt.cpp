#include "frontend/ast/statements/try_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

using namespace llvm;

void TryStatementNode::codegen(nv::IRGenerationContext& ctx) {
    auto& B   = ctx.get_builder();
    auto& M   = ctx.get_module();
    auto& C   = ctx.get_context();

    auto* I8Ptr  = PointerType::getUnqual(C);
    auto* I32    = Type::getInt32Ty(C);
    auto* VoidTy = Type::getVoidTy(C);
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

    auto* push_fn      = ctx.ensure_runtime_func("nv_push_try_handler",    {}, I8Ptr);
    auto* pop_fn       = ctx.ensure_runtime_func("nv_pop_try_handler",     {});
    auto* matches_fn   = ctx.ensure_runtime_func("nv_exception_matches",   {I8Ptr}, I32);
    auto* clear_fn     = ctx.ensure_runtime_func("nv_clear_current_exception", {});
    auto* get_exc_fn   = ctx.ensure_runtime_func("nv_get_exception_message_into", {ValuePtr});
    auto* rethrow_fn   = ctx.ensure_runtime_func("nv_rethrow_current_exception", {});

    // setjmp: marcado como returns_twice para o optimizer
    auto setjmp_callee = M.getOrInsertFunction(
        "setjmp", FunctionType::get(I32, {I8Ptr}, false));
    auto* setjmp_fn = cast<Function>(setjmp_callee.getCallee());
    setjmp_fn->addFnAttr(Attribute::ReturnsTwice);

    auto* handler_ptr   = B.CreateCall(push_fn, {}, "try_handler_ptr");
    auto* setjmp_result = B.CreateCall(setjmp_fn, {handler_ptr}, "setjmp_result");
    auto* is_exception  = B.CreateICmpNE(setjmp_result, ConstantInt::get(I32, 0), "caught");

    auto* try_bb      = ctx.create_block("try_body");
    auto* catch_bb    = ctx.create_block("try_catch");
    auto* finally_bb  = !finally_body.empty() ? ctx.create_block("try_finally") : nullptr;
    auto* merge_bb    = ctx.create_block("try_merge");
    auto* post_bb     = finally_bb ? finally_bb : merge_bb;

    B.CreateCondBr(is_exception, catch_bb, try_bb);

    B.SetInsertPoint(try_bb);
    ctx.enter_scope();
    for (auto& stmt : try_body) {
        if (stmt) stmt->codegen(ctx);
        if (B.GetInsertBlock()->getTerminator()) break;
    }
    ctx.exit_scope();
    if (!B.GetInsertBlock()->getTerminator()) {
        B.CreateCall(pop_fn, {});
        B.CreateBr(post_bb);
    }

    B.SetInsertPoint(catch_bb);

    if (catches.empty()) {
        B.CreateCall(rethrow_fn, {});
        B.CreateUnreachable();
    } else {
        auto* unhandled_bb = ctx.create_block("try_unhandled");

        for (size_t i = 0; i < catches.size(); ++i) {
            auto& clause = catches[i];
            std::string idx  = std::to_string(i);
            auto* body_bb    = ctx.create_block("catch_body_" + idx);
            auto* next_bb    = (i + 1 < catches.size())
                               ? ctx.create_block("catch_next_" + std::to_string(i + 1))
                               : unhandled_bb;

            auto* type_str  = B.CreateGlobalString(clause->exception_type, "exc_type_" + idx);
            auto* matches   = B.CreateCall(matches_fn, {type_str}, "matches_" + idx);
            auto* matched   = B.CreateICmpNE(matches, ConstantInt::get(I32, 0));
            B.CreateCondBr(matched, body_bb, next_bb);

            B.SetInsertPoint(body_bb);
            ctx.enter_scope();

            if (!clause->exception_var.empty()) {
                auto* exc_alloca = ctx.create_alloca(ValueTy, clause->exception_var);
                B.CreateCall(get_exc_fn, {exc_alloca});
                nv::SymbolInfo info(exc_alloca, ValueTy, nullptr, true, false);
                ctx.get_symbol_table().define_symbol(clause->exception_var, info);
            }

            B.CreateCall(clear_fn, {});

            for (auto& stmt : clause->body) {
                if (stmt) stmt->codegen(ctx);
                if (B.GetInsertBlock()->getTerminator()) break;
            }
            ctx.exit_scope();

            if (!B.GetInsertBlock()->getTerminator()) {
                B.CreateBr(post_bb);
            }

            if (i + 1 < catches.size()) {
                B.SetInsertPoint(next_bb);
            }
        }

        B.SetInsertPoint(unhandled_bb);
        B.CreateCall(rethrow_fn, {});
        B.CreateUnreachable();
    }

    if (finally_bb) {
        B.SetInsertPoint(finally_bb);
        ctx.enter_scope();
        for (auto& stmt : finally_body) {
            if (stmt) stmt->codegen(ctx);
            if (B.GetInsertBlock()->getTerminator()) break;
        }
        ctx.exit_scope();
        if (!B.GetInsertBlock()->getTerminator()) {
            B.CreateBr(merge_bb);
        }
    }

    B.SetInsertPoint(merge_bb);
}
