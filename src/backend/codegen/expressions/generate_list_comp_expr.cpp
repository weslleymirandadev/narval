#include "frontend/ast/expressions/list_comp_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void ListCompNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());

    auto& b   = ctx.get_builder();
    auto& c   = ctx.get_context();
    auto& m   = ctx.get_module();

    auto* ValueTy  = nv::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = nv::ir_utils::get_value_ptr(ctx);
    auto* I32      = llvm::Type::getInt32Ty(c);
    auto* VoidTy   = llvm::Type::getVoidTy(c);

    auto* func = ctx.get_current_function();
    if (!func) { ctx.push_value(nullptr); return; }

    // Output vector
    auto* outVec = ctx.create_alloca(ValueTy, "lc.vec");
    {
        auto fn = m.getOrInsertFunction("create_vector",
            llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
        b.CreateCall(llvm::cast<llvm::Function>(fn.getCallee()),
                     {outVec, llvm::ConstantInt::get(I32, 0)});
    }

    auto fn_push = m.getOrInsertFunction("vector_push_method",
        llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, ValuePtr}, false));

    // Helper: box any LLVM value into a Value alloca
    auto box = [&](llvm::Value* v) -> llvm::Value* {
        auto* tmp = ctx.create_alloca(ValueTy, "lc.box");
        if (!v) { b.CreateStore(llvm::UndefValue::get(ValueTy), tmp); return tmp; }
        if (v->getType() == ValueTy) {
            b.CreateStore(v, tmp);
        } else if (v->getType()->isIntegerTy(1)) {
            auto fn = m.getOrInsertFunction("create_bool",
                llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
            b.CreateCall(llvm::cast<llvm::Function>(fn.getCallee()),
                         {tmp, b.CreateZExt(v, I32)});
        } else if (v->getType()->isIntegerTy()) {
            auto fn = m.getOrInsertFunction("create_int",
                llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
            llvm::Value* iv = v->getType()->isIntegerTy(32) ? v
                                : b.CreateSExtOrTrunc(v, I32);
            b.CreateCall(llvm::cast<llvm::Function>(fn.getCallee()), {tmp, iv});
        } else if (v->getType()->isFloatingPointTy()) {
            auto* F64 = llvm::Type::getDoubleTy(c);
            if (v->getType() != F64) v = b.CreateFPExt(v, F64);
            auto fn = m.getOrInsertFunction("create_float",
                llvm::FunctionType::get(VoidTy, {ValuePtr, F64}, false));
            b.CreateCall(llvm::cast<llvm::Function>(fn.getCallee()), {tmp, v});
        } else if (v->getType()->isPointerTy()) {
            auto* i8p = nv::ir_utils::get_i8_ptr(ctx);
            auto fn = m.getOrInsertFunction("create_str",
                llvm::FunctionType::get(VoidTy, {ValuePtr, i8p}, false));
            b.CreateCall(llvm::cast<llvm::Function>(fn.getCallee()), {tmp, v});
        } else {
            b.CreateStore(llvm::UndefValue::get(ValueTy), tmp);
        }
        return tmp;
    };

    if (generators.empty()) { ctx.push_value(b.CreateLoad(ValueTy, outVec)); return; }

    // Evaluate source expression
    auto& gen = generators[0];
    gen.second->codegen(ctx);
    llvm::Value* src = ctx.has_value() ? ctx.pop_value() : nullptr;
    if (!src) { ctx.push_value(b.CreateLoad(ValueTy, outVec)); return; }

    // Determine iteration kind and length
    llvm::Value* len = nullptr;
    llvm::AllocaInst* src_alloca = nullptr;

    enum class SrcKind { Count, ValueStruct } kind;

    if (src->getType()->isIntegerTy()) {
        // Integer literal: iterate [0, N)
        kind = SrcKind::Count;
        if (src->getType() != I32)
            src = nv::ir_utils::promote_type(ctx, src, I32);
        len = src;
    } else if (src->getType() == ValueTy) {
        // Value struct (vector/array): use nv_get_iterable_length
        kind = SrcKind::ValueStruct;
        src_alloca = ctx.create_alloca(ValueTy, "lc.src");
        b.CreateStore(src, src_alloca);
        auto fn_len = m.getOrInsertFunction("nv_get_iterable_length",
            llvm::FunctionType::get(I32, {ValuePtr}, false));
        len = b.CreateCall(llvm::cast<llvm::Function>(fn_len.getCallee()), {src_alloca});
    } else {
        // Unsupported source type: return empty vector
        ctx.push_value(b.CreateLoad(ValueTy, outVec));
        return;
    }

    // Loop blocks
    auto* hdr_bb  = llvm::BasicBlock::Create(c, "lc.hdr",  func);
    auto* body_bb = llvm::BasicBlock::Create(c, "lc.body", func);
    auto* push_bb = llvm::BasicBlock::Create(c, "lc.push", func);
    auto* skip_bb = llvm::BasicBlock::Create(c, "lc.skip", func);
    auto* step_bb = llvm::BasicBlock::Create(c, "lc.step", func);
    auto* exit_bb = llvm::BasicBlock::Create(c, "lc.exit", func);

    auto* i_alloca = ctx.create_alloca(I32, "lc.i");
    b.CreateStore(llvm::ConstantInt::get(I32, 0), i_alloca);
    b.CreateBr(hdr_bb);

    // Header: i < len
    b.SetInsertPoint(hdr_bb);
    auto* i_val = b.CreateLoad(I32, i_alloca, "lc.i");
    b.CreateCondBr(b.CreateICmpSLT(i_val, len, "lc.cnd"), body_bb, exit_bb);

    // Body: bind loop variable, evaluate elt
    b.SetInsertPoint(body_bb);
    ctx.enter_scope();

    // Get element for this iteration
    llvm::Value* elem_loaded = nullptr;
    if (kind == SrcKind::Count) {
        // Element IS the index
        auto* elem_alloca = ctx.create_alloca(ValueTy, "lc.elem");
        auto fn_ci = m.getOrInsertFunction("create_int",
            llvm::FunctionType::get(VoidTy, {ValuePtr, I32}, false));
        b.CreateCall(llvm::cast<llvm::Function>(fn_ci.getCallee()),
                     {elem_alloca, b.CreateLoad(I32, i_alloca)});
        elem_loaded = b.CreateLoad(ValueTy, elem_alloca);
    } else {
        // Get element from vector/array
        auto* elem_alloca = ctx.create_alloca(ValueTy, "lc.elem");
        auto fn_get = m.getOrInsertFunction("array_get_index_v",
            llvm::FunctionType::get(VoidTy, {ValuePtr, ValuePtr, I32}, false));
        b.CreateCall(llvm::cast<llvm::Function>(fn_get.getCallee()),
                     {elem_alloca, src_alloca, b.CreateLoad(I32, i_alloca)});
        elem_loaded = b.CreateLoad(ValueTy, elem_alloca);
    }

    // Bind loop variable in symbol table
    if (gen.first && gen.first->kind == NodeType::Identifier) {
        auto* id = static_cast<IdentifierNode*>(gen.first.get());
        auto* var = ctx.create_and_register_variable(id->symbol, ValueTy, nullptr, false);
        if (elem_loaded) b.CreateStore(elem_loaded, var);
    }

    // Evaluate if_cond (filter or conditional element)
    if (if_cond) {
        if_cond->codegen(ctx);
        llvm::Value* cv = ctx.has_value() ? ctx.pop_value() : nullptr;
        if (!cv) cv = llvm::ConstantInt::getFalse(c);
        // Normalize to i1
        if (!cv->getType()->isIntegerTy(1)) {
            if (cv->getType() == ValueTy) {
                // Extract tag from Value struct: tag == 0 means falsy (None/null)
                auto* tag = b.CreateExtractValue(cv, {0}, "lc.tag");
                cv = b.CreateICmpNE(tag, llvm::Constant::getNullValue(tag->getType()), "lc.bool");
            } else if (cv->getType()->isIntegerTy()) {
                cv = b.CreateICmpNE(cv, llvm::ConstantInt::get(cv->getType(), 0), "lc.bool");
            } else if (cv->getType()->isFloatingPointTy()) {
                cv = b.CreateFCmpONE(cv, llvm::ConstantFP::get(cv->getType(), 0.0), "lc.bool");
            } else {
                cv = llvm::ConstantInt::getTrue(c);
            }
        }
        if (else_expr) {
            // Conditional element: push elt when true, else_expr when false
            b.CreateCondBr(cv, push_bb, skip_bb);
        } else {
            // Filter: only push when true
            b.CreateCondBr(cv, push_bb, skip_bb);
        }
    } else {
        b.CreateBr(push_bb);
    }

    // Push block: evaluate element expression and push to vector
    b.SetInsertPoint(push_bb);
    llvm::Value* elt_val = nullptr;
    if (elt) {
        elt->codegen(ctx);
        elt_val = ctx.has_value() ? ctx.pop_value() : nullptr;
    }
    {
        auto* boxed = box(elt_val);
        auto* tmp_out = ctx.create_alloca(ValueTy, "lc.push.out");
        b.CreateCall(llvm::cast<llvm::Function>(fn_push.getCallee()),
                     {tmp_out, outVec, boxed});
    }
    b.CreateBr(step_bb);

    // Skip block: push else_expr (if present) or skip entirely
    b.SetInsertPoint(skip_bb);
    if (else_expr && if_cond) {
        llvm::Value* else_val = nullptr;
        else_expr->codegen(ctx);
        else_val = ctx.has_value() ? ctx.pop_value() : nullptr;
        auto* boxed = box(else_val);
        auto* tmp_out = ctx.create_alloca(ValueTy, "lc.else.out");
        b.CreateCall(llvm::cast<llvm::Function>(fn_push.getCallee()),
                     {tmp_out, outVec, boxed});
    }
    b.CreateBr(step_bb);

    // Step: i++, exit scope
    b.SetInsertPoint(step_bb);
    ctx.exit_scope();
    b.CreateStore(
        b.CreateAdd(b.CreateLoad(I32, i_alloca), llvm::ConstantInt::get(I32, 1)),
        i_alloca);
    b.CreateBr(hdr_bb);

    // Exit
    b.SetInsertPoint(exit_bb);
    ctx.push_value(b.CreateLoad(ValueTy, outVec));
}
