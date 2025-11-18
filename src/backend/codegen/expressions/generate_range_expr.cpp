#include "frontend/ast/expressions/range_expr_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/DerivedTypes.h>

void RangeExprNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    auto& c = ctx.get_context();
    auto& m = ctx.get_module();

    // Evaluate bounds
    llvm::Type* I32 = llvm::Type::getInt32Ty(c);
    if (start) start->codegen(ctx);
    llvm::Value* s = ctx.has_value() ? ctx.pop_value() : nullptr;
    if (end) end->codegen(ctx);
    llvm::Value* e = ctx.has_value() ? ctx.pop_value() : nullptr;

    if (!s || !e) {
        throw std::runtime_error("range expression requires both start and end");
    }

    if (s->getType() != I32) s = rph::ir_utils::promote_type(ctx, s, I32);
    if (e->getType() != I32) e = rph::ir_utils::promote_type(ctx, e, I32);

    // Prepare runtime types
    auto* ValueTy  = rph::ir_utils::get_value_struct(ctx);
    auto* ValuePtr = rph::ir_utils::get_value_ptr(ctx);

    // out vector value aggregate
    auto* outVec = ctx.create_alloca(ValueTy, "range.vec");

    // create_vector(Value* out, i32 capacity)
    auto decl_create_vector = m.getOrInsertFunction(
        "create_vector",
        llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I32}, false)
    );

    // vector_push_method(Value* out, Value* self, Value* elem)
    auto decl_push = m.getOrInsertFunction(
        "vector_push_method",
        llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, ValuePtr, ValuePtr}, false)
    );

    // Estimate capacity = max(0, (e - s) + (inclusive ? 1 : 0))
    llvm::Value* diff = b.CreateSub(e, s, "range.diff");
    if (inclusive) {
        diff = b.CreateAdd(diff, llvm::ConstantInt::get(I32, 1));
    }
    // clamp negative to 0
    llvm::Value* isNeg = b.CreateICmpSLT(diff, llvm::ConstantInt::get(I32, 0));
    llvm::Value* cap = b.CreateSelect(isNeg, llvm::ConstantInt::get(I32, 0), diff, "range.cap");

    b.CreateCall(llvm::cast<llvm::Function>(decl_create_vector.getCallee()), {outVec, cap});

    // Build for-like loop i from s to e (<= if inclusive else <)
    auto* func = ctx.get_current_function();
    if (!func) {
        // Ensure we are inside a function; required for blocks
        throw std::runtime_error("range expression codegen requires a current function context");
    }

    auto* header_bb = llvm::BasicBlock::Create(c, "range.header", func);
    auto* body_bb   = llvm::BasicBlock::Create(c, "range.body",   func);
    auto* step_bb   = llvm::BasicBlock::Create(c, "range.step",   func);
    auto* exit_bb   = llvm::BasicBlock::Create(c, "range.exit",   func);

    // i variable
    auto* i_alloca = ctx.create_alloca(I32, "range.i");
    b.CreateStore(s, i_alloca);
    b.CreateBr(header_bb);

    // header: compare
    b.SetInsertPoint(header_bb);
    auto* i_val = b.CreateLoad(I32, i_alloca, "i.val");
    llvm::Value* cond = inclusive
        ? (llvm::Value*)b.CreateICmpSLE(i_val, e, "range.cond")
        : (llvm::Value*)b.CreateICmpSLT(i_val, e, "range.cond");
    b.CreateCondBr(cond, body_bb, exit_bb);

    // body: box i and push
    b.SetInsertPoint(body_bb);
    auto* boxed = ctx.create_alloca(ValueTy, "range.elem");
    // create_int(Value* out, i32)
    auto decl_create_int = m.getOrInsertFunction(
        "create_int",
        llvm::FunctionType::get(llvm::Type::getVoidTy(c), {ValuePtr, I32}, false)
    );
    b.CreateCall(llvm::cast<llvm::Function>(decl_create_int.getCallee()), {boxed, b.CreateLoad(I32, i_alloca)});

    // push into vector: vector_push_method(tmp_out, outVec, boxed)
    auto* tmp_out = ctx.create_alloca(ValueTy, "tmp.out");
    b.CreateCall(llvm::cast<llvm::Function>(decl_push.getCallee()), {tmp_out, outVec, boxed});

    b.CreateBr(step_bb);

    // step: i = i + 1
    b.SetInsertPoint(step_bb);
    auto* next = b.CreateAdd(b.CreateLoad(I32, i_alloca), llvm::ConstantInt::get(I32, 1), "inc");
    b.CreateStore(next, i_alloca);
    b.CreateBr(header_bb);

    // exit: return the vector Value
    b.SetInsertPoint(exit_bb);
    ctx.push_value(b.CreateLoad(ValueTy, outVec));
}
