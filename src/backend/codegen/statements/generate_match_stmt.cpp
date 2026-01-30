#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/DerivedTypes.h>

static llvm::Value* build_match_condition(rph::IRGenerationContext& ctx, Expr* pattern, llvm::Value* target_val) {
    auto& b = ctx.get_builder();
    auto& c = ctx.get_context();

    if (!pattern) return nullptr;

    // OR pattern: compose recursively
    if (auto* bin = dynamic_cast<BinaryExprNode*>(pattern)) {
        if (bin->op == "||") {
            auto* lhs_clone = static_cast<Expr*>(bin->left->clone());
            auto* rhs_clone = static_cast<Expr*>(bin->right->clone());
            auto* lhs_cond = build_match_condition(ctx, lhs_clone, target_val);
            auto* rhs_cond = build_match_condition(ctx, rhs_clone, target_val);
            delete lhs_clone;
            delete rhs_clone;
            if (!lhs_cond || !rhs_cond) return nullptr;
            return b.CreateOr(lhs_cond, rhs_cond, "match.or");
        }
    }

    // Range pattern: start..end or start..=end (numeric int32)
    if (auto* rng = dynamic_cast<RangeExprNode*>(pattern)) {
        // Evaluate bounds
        if (rng->start) rng->start->codegen(ctx); else return nullptr;
        llvm::Value* s = ctx.pop_value();
        if (rng->end) rng->end->codegen(ctx); else return nullptr;
        llvm::Value* e = ctx.pop_value();
        if (!s || !e || !target_val) return nullptr;

        auto* I32 = llvm::Type::getInt32Ty(c);
        if (target_val->getType() != I32) target_val = rph::ir_utils::promote_type(ctx, target_val, I32);
        if (s->getType() != I32) s = rph::ir_utils::promote_type(ctx, s, I32);
        if (e->getType() != I32) e = rph::ir_utils::promote_type(ctx, e, I32);

        auto* ge = b.CreateICmpSGE(target_val, s, "in.ge");
        llvm::Value* end_cmp = rng->inclusive
            ? (llvm::Value*)b.CreateICmpSLE(target_val, e, "in.le")
            : (llvm::Value*)b.CreateICmpSLT(target_val, e, "in.lt");
        return b.CreateAnd(ge, end_cmp, "in.range");
    }

    // Identifier 'default' or '_' is handled at a higher level (no condition)
    if (auto* id = dynamic_cast<IdentifierNode*>(pattern)) {
        if (id->symbol == "default" || id->symbol == "_") {
            return nullptr; // no-op here, handled at match level
        }
    }

    // Fallback: equality comparison with target
    pattern->codegen(ctx);
    llvm::Value* v = ctx.pop_value();
    if (!v || !target_val) return nullptr;
    
    // Verificar se há incompatibilidade de tipos (string vs int)
    auto* i8p = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(c));
    auto* i32 = llvm::Type::getInt32Ty(c);
    
    // Se target é string (i8*) e pattern é int, converter string para int
    // Isso acontece quando read() retorna string e comparamos com números
    if (target_val->getType() == i8p && v->getType() == i32) {
        // Chamar atoi para converter string para int
        auto* atoi_ty = llvm::FunctionType::get(i32, {i8p}, false);
        auto* atoi_fn = llvm::cast<llvm::Function>(
            ctx.get_module().getOrInsertFunction("atoi", atoi_ty).getCallee()
        );
        llvm::Value* target_int = b.CreateCall(atoi_fn, {target_val}, "strtoint");
        // Agora ambos são i32, fazer comparação direta
        return b.CreateICmpEQ(target_int, v, "matcheq");
    }
    
    // Se target é int e pattern é string, converter pattern para int
    if (target_val->getType() == i32 && v->getType() == i8p) {
        auto* atoi_ty = llvm::FunctionType::get(i32, {i8p}, false);
        auto* atoi_fn = llvm::cast<llvm::Function>(
            ctx.get_module().getOrInsertFunction("atoi", atoi_ty).getCallee()
        );
        llvm::Value* pattern_int = b.CreateCall(atoi_fn, {v}, "strtoint");
        return b.CreateICmpEQ(target_val, pattern_int, "matcheq");
    }
    
    // Caso padrão: usar create_comparison que já trata outros casos (int/float, string/string, etc)
    return rph::ir_utils::create_comparison(ctx, target_val, v, "==");
}

void MatchStmtNode::codegen(rph::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    auto& b = ctx.get_builder();
    auto& c = ctx.get_context();

    if (!target) throw std::runtime_error("match without target");

    // Evaluate target once and store in alloca to reuse
    target->codegen(ctx);
    llvm::Value* tgt = ctx.pop_value();
    if (!tgt) throw std::runtime_error("match target produced no value");

    llvm::AllocaInst* tgt_alloca = nullptr;
    if (llvm::isa<llvm::AllocaInst>(tgt)) {
        tgt_alloca = llvm::cast<llvm::AllocaInst>(tgt);
    } else {
        tgt_alloca = ctx.create_alloca(tgt->getType(), "match.tgt");
        b.CreateStore(tgt, tgt_alloca);
    }

    auto* func = ctx.get_current_function();
    if (!func) throw std::runtime_error("match codegen requires current function");

    // Final merge block after executing a case
    auto* after_bb = llvm::BasicBlock::Create(c, "match.after", func);

    // Build chain of case tests
    llvm::BasicBlock* next_test_bb = llvm::BasicBlock::Create(c, "match.entry", func);
    b.CreateBr(next_test_bb);

    bool has_default = false;
    size_t default_index = (size_t)-1;

    for (size_t i = 0; i < cases.size(); ++i) {
        // Determine if this is a default case: Identifier("default") or Identifier("_")
        bool is_default = false;
        if (auto* id = dynamic_cast<IdentifierNode*>(cases[i].get())) {
            if (id->symbol == "default" || id->symbol == "_") is_default = true;
        }

        // Create blocks for this case
        auto* this_test_bb = next_test_bb;
        auto* then_bb = llvm::BasicBlock::Create(c, "match.case.then", func);
        auto* cont_bb = (i + 1 < cases.size()) ? llvm::BasicBlock::Create(c, "match.next", func) : nullptr;
        if (!cont_bb) cont_bb = llvm::BasicBlock::Create(c, "match.endtests", func);

        // Set insertion to current test block
        b.SetInsertPoint(this_test_bb);

        if (is_default) {
            // Jump directly to then
            b.CreateBr(then_bb);
        } else {
            // Load target value for comparison
            llvm::Value* tgt_val = b.CreateLoad(tgt_alloca->getAllocatedType(), tgt_alloca, "match.tgt.val");
            llvm::Value* cond = build_match_condition(ctx, cases[i].get(), tgt_val);
            if (!cond) {
                // If condition could not be built, treat as false and continue
                cond = llvm::ConstantInt::getFalse(c);
            } else if (!cond->getType()->isIntegerTy(1)) {
                // Normalize to i1
                cond = b.CreateICmpNE(cond, llvm::ConstantInt::get(cond->getType(), 0), "tobool");
            }
            b.CreateCondBr(cond, then_bb, cont_bb);
        }

        // Emit body
        b.SetInsertPoint(then_bb);
        ctx.enter_scope();
        for (auto& stmt : bodies[i]) {
            if (stmt) stmt->codegen(ctx);
        }
        ctx.exit_scope();
        if (!b.GetInsertBlock()->getTerminator()) b.CreateBr(after_bb);

        // Prepare next test
        b.SetInsertPoint(cont_bb);
        next_test_bb = cont_bb;

        if (is_default) { has_default = true; default_index = i; }
    }

    // No case matched path: just branch to after
    if (!b.GetInsertBlock()->getTerminator()) {
        b.CreateBr(after_bb);
    }

    // Continue after match
    b.SetInsertPoint(after_bb);
}
