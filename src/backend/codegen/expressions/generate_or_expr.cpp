#include "frontend/ast/expressions/or_expr_node.hpp"
#include "frontend/ast/statements/propagate_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>

// PropagateStmtNode codegen: re-throw the current `err` variable as a Narval exception.
void PropagateStmtNode::codegen(nv::IRGenerationContext& ctx) {
    auto& B       = ctx.get_builder();
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);

    // Look up the `err` slot in scope
    auto err_sym = ctx.get_symbol_table().lookup_symbol("err");
    llvm::Value* err_ptr = nullptr;
    if (err_sym.has_value() && err_sym->value) {
        err_ptr = err_sym->value;
    } else {
        // No err in scope — create a generic error
        auto* slot = ctx.create_alloca(ValueTy, "propagate_err");
        auto* msg  = B.CreateGlobalStringPtr("propagation error");
        auto* fn   = ctx.ensure_runtime_func("create_error",
            {ValuePtr, nv::ir_utils::get_i8_ptr(ctx)});
        B.CreateCall(fn, {slot, msg});
        err_ptr = slot;
    }

    // Throw via the Narval exception mechanism
    auto* throw_fn = ctx.ensure_runtime_func("nv_throw_exception", {ValuePtr});
    B.CreateCall(throw_fn, {err_ptr});
    // Unreachable after throw, but we need a terminator
    B.CreateUnreachable();
}

// Helper: box a raw LLVM value into a narval Value struct
static llvm::Value* box_to_value(nv::IRGenerationContext& ctx, llvm::Value* val) {
    if (!val) return nullptr;
    auto& B       = ctx.get_builder();
    auto& C       = ctx.get_context();
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I32      = llvm::Type::getInt32Ty(C);
    auto* F64      = llvm::Type::getDoubleTy(C);
    auto* I8P      = nv::ir_utils::get_i8_ptr(ctx);

    if (val->getType() == ValueTy) return val;  // already boxed

    auto* slot = ctx.create_alloca(ValueTy, "or_box");
    if (val->getType()->isIntegerTy(1)) {
        auto* fn = ctx.ensure_runtime_func("create_bool", {ValuePtr, I32});
        B.CreateCall(fn, {slot, B.CreateZExt(val, I32)});
    } else if (val->getType()->isIntegerTy()) {
        auto* fn = ctx.ensure_runtime_func("create_int", {ValuePtr, I32});
        auto* iv = val->getType()->isIntegerTy(32) ? val : B.CreateSExtOrTrunc(val, I32);
        B.CreateCall(fn, {slot, iv});
    } else if (val->getType()->isFloatingPointTy()) {
        auto* fn = ctx.ensure_runtime_func("create_float", {ValuePtr, F64});
        auto* fv = val->getType() == F64 ? val : B.CreateFPExt(val, F64);
        B.CreateCall(fn, {slot, fv});
    } else if (val->getType() == I8P) {
        auto* fn = ctx.ensure_runtime_func("create_str", {ValuePtr, I8P});
        B.CreateCall(fn, {slot, val});
    } else {
        // Fallback: store as-is if pointer-like, else undef
        B.CreateStore(llvm::UndefValue::get(ValueTy), slot);
    }
    return B.CreateLoad(ValueTy, slot, "or_boxed");
}

// OrExprNode codegen — uses an alloca as result slot (avoids phi-node type issues)
void OrExprNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    auto& C        = ctx.get_context();
    auto& B        = ctx.get_builder();
    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I32      = llvm::Type::getInt32Ty(C);

    // Alloca to hold the final result — allocated in entry block
    auto* result_slot = ctx.create_alloca(ValueTy, "or_result");

    // --- 1. Evaluate the base expression ---
    expr->codegen(ctx);
    llvm::Value* base_val = ctx.pop_value();

    auto* base_slot = ctx.create_alloca(ValueTy, "or_base");
    if (base_val && base_val->getType() == ValueTy)
        B.CreateStore(base_val, base_slot);

    // --- 2. nv_is_failure(base_slot) ---
    auto* fail_fn  = ctx.ensure_runtime_func("nv_is_failure", {ValuePtr}, I32);
    auto* fail_i32 = B.CreateCall(fail_fn, {base_slot}, "or.fail");
    auto* is_fail  = B.CreateICmpNE(fail_i32, llvm::ConstantInt::get(I32, 0), "or.cond");

    llvm::Function* fn = ctx.get_current_function();
    auto* succ_bb  = llvm::BasicBlock::Create(C, "or.succ",  fn);
    auto* fail_bb  = llvm::BasicBlock::Create(C, "or.fail",  fn);
    auto* merge_bb = llvm::BasicBlock::Create(C, "or.merge", fn);

    B.CreateCondBr(is_fail, fail_bb, succ_bb);

    // --- 3. Success path: unwrap and store ---
    B.SetInsertPoint(succ_bb);
    auto* unwrap_fn = ctx.ensure_runtime_func("nv_unwrap_inner", {ValuePtr, ValuePtr});
    B.CreateCall(unwrap_fn, {result_slot, base_slot});
    B.CreateBr(merge_bb);

    // --- 4. Failure path: bind err, run handler ---
    B.SetInsertPoint(fail_bb);

    auto* err_slot   = ctx.create_alloca(ValueTy, "err");
    auto* get_err_fn = ctx.ensure_runtime_func("nv_get_failure_err", {ValuePtr, ValuePtr});
    B.CreateCall(get_err_fn, {err_slot, base_slot});

    ctx.enter_scope();
    nv::SymbolInfo err_info(err_slot, ValueTy, nullptr, true, false);
    ctx.get_symbol_table().define_symbol("err", err_info);

    // Push or-block context so `return` inside the block redirects here
    ctx.push_or_block(llvm::cast<llvm::AllocaInst>(result_slot), merge_bb);

    if (is_block_handler) {
        for (const auto& stmt : block_stmts) {
            if (stmt) {
                stmt->codegen(ctx);
                // Discard any leftover stack values after each statement
                while (ctx.has_value()) ctx.pop_value();
                if (B.GetInsertBlock()->getTerminator()) break;
            }
        }
    } else if (value_handler) {
        // Value handler: box the expression and store it as the result
        value_handler->codegen(ctx);
        llvm::Value* handler_val = ctx.has_value() ? ctx.pop_value() : nullptr;
        llvm::Value* boxed = box_to_value(ctx, handler_val);
        if (boxed) B.CreateStore(boxed, result_slot);
    }

    ctx.pop_or_block();
    ctx.exit_scope();

    // Branch to merge if not already terminated by return/throw/propagate
    if (!B.GetInsertBlock()->getTerminator()) {
        B.CreateBr(merge_bb);
    }

    // --- 5. Merge: load result ---
    B.SetInsertPoint(merge_bb);
    ctx.push_value(B.CreateLoad(ValueTy, result_slot, "or.final"));
}
