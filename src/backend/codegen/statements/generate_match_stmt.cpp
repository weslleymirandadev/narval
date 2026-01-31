#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/DerivedTypes.h>

static llvm::Value* build_match_condition(nv::IRGenerationContext& ctx, Expr* pattern, llvm::Value* target_val) {
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

    // Range pattern: start..end or start..=end (numeric int32 or single-char strings)
    if (auto* rng = dynamic_cast<RangeExprNode*>(pattern)) {
        // Evaluate bounds
        if (rng->start) rng->start->codegen(ctx); else return nullptr;
        llvm::Value* s = ctx.pop_value();
        if (rng->end) rng->end->codegen(ctx); else return nullptr;
        llvm::Value* e = ctx.pop_value();
        if (!s || !e || !target_val) return nullptr;

        auto* I32 = llvm::Type::getInt32Ty(c);
        auto* I8P = llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(c));
        auto* I8 = llvm::Type::getInt8Ty(c);

        // Verificar se é range numérico ou de caracteres
        bool target_is_string = target_val->getType() == I8P;
        bool start_is_string = s->getType() == I8P;
        bool end_is_string = e->getType() == I8P;

        if (target_is_string && start_is_string && end_is_string) {
            // Range de caracteres: comparar primeiro caractere de cada string
            // Strings são i8* (ponteiros para char), então carregamos o primeiro byte
            
            // target[0] - i8* já aponta para o primeiro byte
            auto* target_char_ptr = b.CreateGEP(I8, target_val, {b.getInt32(0)}, "target.char.ptr");
            auto* target_char = b.CreateLoad(I8, target_char_ptr, "target.char.val");
            
            // start[0]
            auto* start_char_ptr = b.CreateGEP(I8, s, {b.getInt32(0)}, "start.char.ptr");
            auto* start_char = b.CreateLoad(I8, start_char_ptr, "start.char.val");
            
            // end[0]
            auto* end_char_ptr = b.CreateGEP(I8, e, {b.getInt32(0)}, "end.char.ptr");
            auto* end_char = b.CreateLoad(I8, end_char_ptr, "end.char.val");
            
            if (!target_char || !start_char || !end_char) return nullptr;
            
            // Converter para i32 para comparação (zero-extend para caracteres unsigned)
            // Garantir que todos são i8 antes de converter
            llvm::Value* target_i32 = nullptr;
            llvm::Value* start_i32 = nullptr;
            llvm::Value* end_i32 = nullptr;
            
            // Converter target_char para i32
            if (target_char->getType() == I8) {
                target_i32 = b.CreateZExt(target_char, I32, "target.i32");
            } else if (target_char->getType() == I32) {
                target_i32 = target_char;
            } else if (target_char->getType()->isIntegerTy()) {
                // Se for outro tipo inteiro, converter para i32
                target_i32 = b.CreateIntCast(target_char, I32, false, "target.i32");
            } else {
                return nullptr;
            }
            
            // Converter start_char para i32
            if (start_char->getType() == I8) {
                start_i32 = b.CreateZExt(start_char, I32, "start.i32");
            } else if (start_char->getType() == I32) {
                start_i32 = start_char;
            } else if (start_char->getType()->isIntegerTy()) {
                start_i32 = b.CreateIntCast(start_char, I32, false, "start.i32");
            } else {
                return nullptr;
            }
            
            // Converter end_char para i32
            if (end_char->getType() == I8) {
                end_i32 = b.CreateZExt(end_char, I32, "end.i32");
            } else if (end_char->getType() == I32) {
                end_i32 = end_char;
            } else if (end_char->getType()->isIntegerTy()) {
                end_i32 = b.CreateIntCast(end_char, I32, false, "end.i32");
            } else {
                return nullptr;
            }
            
            // Verificar que todos são I32 antes de comparar
            if (!target_i32 || !start_i32 || !end_i32) return nullptr;
            if (target_i32->getType() != I32 || start_i32->getType() != I32 || end_i32->getType() != I32) {
                return nullptr;
            }
            
            // target >= start && target <= end (ou < end se não inclusivo)
            auto* ge = b.CreateICmpSGE(target_i32, start_i32, "in.ge");
            llvm::Value* end_cmp = rng->inclusive
                ? (llvm::Value*)b.CreateICmpSLE(target_i32, end_i32, "in.le")
                : (llvm::Value*)b.CreateICmpSLT(target_i32, end_i32, "in.lt");
            return b.CreateAnd(ge, end_cmp, "in.range");
        } else {
            // Range numérico: converter tudo para i32
            llvm::Value* target_i32_val = target_val;
            llvm::Value* s_i32 = s;
            llvm::Value* e_i32 = e;
            
            // Converter target para i32
            // Se target é string (i8*) e range é numérico, converter string para int usando atoi
            if (target_val->getType() == I8P && !start_is_string && !end_is_string) {
                // Target é string, mas range é numérico - converter string para int
                auto* atoi_ty = llvm::FunctionType::get(I32, {I8P}, false);
                auto* atoi_fn = llvm::cast<llvm::Function>(
                    ctx.get_module().getOrInsertFunction("atoi", atoi_ty).getCallee()
                );
                target_i32_val = b.CreateCall(atoi_fn, {target_val}, "strtoint");
            } else if (target_val->getType() != I32) {
                target_i32_val = nv::ir_utils::promote_type(ctx, target_val, I32);
            }
            if (!target_i32_val || target_i32_val->getType() != I32) {
                // Se promote_type falhou, tentar conversão direta
                if (target_val->getType()->isIntegerTy()) {
                    target_i32_val = b.CreateIntCast(target_val, I32, true, "target.i32");
                } else {
                    return nullptr;
                }
            }
            
            // Converter start para i32
            if (s->getType() != I32) {
                s_i32 = nv::ir_utils::promote_type(ctx, s, I32);
            }
            if (!s_i32 || s_i32->getType() != I32) {
                if (s->getType()->isIntegerTy()) {
                    s_i32 = b.CreateIntCast(s, I32, true, "start.i32");
                } else {
                    return nullptr;
                }
            }
            
            // Converter end para i32
            if (e->getType() != I32) {
                e_i32 = nv::ir_utils::promote_type(ctx, e, I32);
            }
            if (!e_i32 || e_i32->getType() != I32) {
                if (e->getType()->isIntegerTy()) {
                    e_i32 = b.CreateIntCast(e, I32, true, "end.i32");
                } else {
                    return nullptr;
                }
            }
            
            // Garantir que todos são I32
            if (target_i32_val->getType() != I32 || s_i32->getType() != I32 || e_i32->getType() != I32) {
                return nullptr;
            }

            auto* ge = b.CreateICmpSGE(target_i32_val, s_i32, "in.ge");
            llvm::Value* end_cmp = rng->inclusive
                ? (llvm::Value*)b.CreateICmpSLE(target_i32_val, e_i32, "in.le")
                : (llvm::Value*)b.CreateICmpSLT(target_i32_val, e_i32, "in.lt");
            return b.CreateAnd(ge, end_cmp, "in.range");
        }
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
    return nv::ir_utils::create_comparison(ctx, target_val, v, "==");
}

void MatchStmtNode::codegen(nv::IRGenerationContext& ctx) {
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
