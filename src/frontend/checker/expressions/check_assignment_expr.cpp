#include "frontend/checker/expressions/check_assignment_expr.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_assignment_expr(nv::Checker* ch, Node* node) {
    static thread_local std::shared_ptr<nv::Type> result;
    const auto* assign = static_cast<AssignmentExprNode*>(node);
    auto left_type = ch->infer_expr(assign->target.get());
    auto right_type = ch->infer_expr(assign->value.get());

    left_type = ch->unify_ctx.resolve(left_type);
    right_type = ch->unify_ctx.resolve(right_type);

    bool left_is_int = left_type->kind == nv::Kind::INT;
    bool left_is_float = left_type->kind == nv::Kind::FLOAT;
    bool right_is_int = right_type->kind == nv::Kind::INT;
    bool right_is_float = right_type->kind == nv::Kind::FLOAT;

    if (left_is_int && right_is_float) {
        left_type = ch->gettyptr("float");
    } else if (left_is_float && right_is_int) {
        right_type = ch->gettyptr("float");
    }

    try {
        ch->unify_ctx.unify(left_type, right_type);
    } catch (std::runtime_error& e) {
        ch->error(node, "Assignment type error: " + std::string(e.what()));
        result = ch->gettyptr("void");
        return result;
    }

    result = ch->unify_ctx.resolve(right_type);
    return result;
}
