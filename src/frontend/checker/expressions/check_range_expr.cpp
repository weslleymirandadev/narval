#include "frontend/checker/expressions/check_range_expr.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_range_expr(nv::Checker* ch, Node* node) {
    auto* range_expr = static_cast<RangeExprNode*>(node);
    
    if (!range_expr->start || !range_expr->end) {
        ch->error(node, "Range expression requires both start and end");
        return ch->gettyptr("void");
    }
    
    // Verificar tipos dos limites
    auto start_type = ch->infer_expr(range_expr->start.get());
    auto end_type = ch->infer_expr(range_expr->end.get());
    
    start_type = ch->unify_ctx.resolve(start_type);
    end_type = ch->unify_ctx.resolve(end_type);
    
    // Verificar compatibilidade de tipos
    bool start_is_int = start_type->kind == nv::Kind::INT;
    bool start_is_string = start_type->kind == nv::Kind::STRING;
    bool end_is_int = end_type->kind == nv::Kind::INT;
    bool end_is_string = end_type->kind == nv::Kind::STRING;
    
    if ((start_is_int && !end_is_int) || (start_is_string && !end_is_string)) {
        ch->error(range_expr->end.get(), 
                  "Range bounds must have compatible types");
        return ch->gettyptr("void");
    }
    
    if (!start_is_int && !start_is_string) {
        ch->error(range_expr->start.get(), 
                  "Range bounds must be integers or strings");
        return ch->gettyptr("void");
    }
    
    // Unificar tipos
    try {
        ch->unify_ctx.unify(start_type, end_type);
    } catch (std::runtime_error& e) {
        ch->error(range_expr->end.get(), 
                  "Range bounds have incompatible types");
        return ch->gettyptr("void");
    }
    
    // Range expression não retorna um tipo diretamente (é usado em for/match)
    // Retornar void por enquanto
    return ch->gettyptr("void");
}
