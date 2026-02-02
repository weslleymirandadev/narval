#include "frontend/checker/expressions/check_conditional_expr.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_conditional_expr(nv::Checker* ch, Node* node) {
    static thread_local std::shared_ptr<nv::Type> temp_result;
    auto* cond_expr = static_cast<ConditionalExprNode*>(node);
    
    if (!cond_expr->condition) {
        ch->error(node, "Conditional expression requires a condition");
        return ch->gettyptr("void");
    }
    
    if (!cond_expr->true_expr || !cond_expr->false_expr) {
        ch->error(node, "Conditional expression requires both true and false branches");
        return ch->gettyptr("void");
    }
    
    // Verificar condição
    auto cond_type = ch->infer_expr(cond_expr->condition.get());
    cond_type = ch->unify_ctx.resolve(cond_type);
    
    // Verificar que a condição é bool
    try {
        ch->unify_ctx.unify(cond_type, ch->gettyptr("bool"));
    } catch (std::runtime_error& e) {
        ch->error(cond_expr->condition.get(), 
                  "Conditional expression condition must be of type 'bool', but got '" + cond_type->toString() + "'");
        return ch->gettyptr("void");
    }
    
    // Verificar tipos das branches
    auto true_type = ch->infer_expr(cond_expr->true_expr.get());
    auto false_type = ch->infer_expr(cond_expr->false_expr.get());
    
    true_type = ch->unify_ctx.resolve(true_type);
    false_type = ch->unify_ctx.resolve(false_type);
    
    // Verificar coerção implícita int -> float
    bool true_is_int = true_type->kind == nv::Kind::INT;
    bool true_is_float = true_type->kind == nv::Kind::FLOAT;
    bool false_is_int = false_type->kind == nv::Kind::INT;
    bool false_is_float = false_type->kind == nv::Kind::FLOAT;
    
    if (true_is_int && false_is_float) {
        true_type = ch->gettyptr("float");
    } else if (true_is_float && false_is_int) {
        false_type = ch->gettyptr("float");
    }
    
    // Unificar tipos das branches
    try {
        ch->unify_ctx.unify(true_type, false_type);
    } catch (std::runtime_error& e) {
        ch->error(cond_expr->true_expr.get(), 
                  "Conditional expression branches have incompatible types: '" + 
                  true_type->toString() + "' and '" + false_type->toString() + "'");
        return ch->gettyptr("void");
    }
    
    // Retornar tipo unificado
    temp_result = ch->unify_ctx.resolve(true_type);
    return temp_result;
}
