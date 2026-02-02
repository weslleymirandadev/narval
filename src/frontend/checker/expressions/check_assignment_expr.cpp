#include "frontend/checker/expressions/check_assignment_expr.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type> check_assignment_expr(nv::Checker* ch, Node* node) {
    const auto* assign = static_cast<AssignmentExprNode*>(node);
    auto left_type = ch->infer_expr(assign->target.get());
    auto right_type = ch->infer_expr(assign->value.get());
    
    // Resolver tipos antes de unificar
    left_type = ch->unify_ctx.resolve(left_type);
    right_type = ch->unify_ctx.resolve(right_type);
    
    // Verificar coerção implícita int -> float
    bool left_is_int = left_type->kind == nv::Kind::INT;
    bool left_is_float = left_type->kind == nv::Kind::FLOAT;
    bool right_is_int = right_type->kind == nv::Kind::INT;
    bool right_is_float = right_type->kind == nv::Kind::FLOAT;
    
    // Se um é int e outro é float, promover int para float
    if (left_is_int && right_is_float) {
        left_type = ch->gettyptr("float");
    } else if (left_is_float && right_is_int) {
        right_type = ch->gettyptr("float");
    }
    
    try {
        ch->unify_ctx.unify(left_type, right_type);
    } catch (std::runtime_error& e) {
        // Usar error() ao invés de throw para evitar duplicação de erros
        ch->error(node, "Assignment type error: " + std::string(e.what()));
        return ch->gettyptr("void");
    }
    
    // Resolver tipo após unificação
    right_type = ch->unify_ctx.resolve(right_type);
    return right_type;
}
