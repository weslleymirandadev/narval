#include "frontend/checker/expressions/check_await_expr.hpp"
#include "frontend/ast/expressions/await_expr_node.hpp"
#include "frontend/checker/type.hpp"

std::shared_ptr<nv::Type> check_await_expr(nv::Checker* checker, Node* node) {
    auto* expr = static_cast<AwaitExprNode*>(node);

    if (checker->no_std_attr_node) {
        checker->no_std_error(node, "await");
        return checker->gettyptr("None");
    }

    auto operand_ty = checker->check_node(expr->operand.get());

    // await Future<T> → T
    if (operand_ty && operand_ty->kind == nv::Kind::FUTURE) {
        auto* fut = static_cast<nv::Future*>(operand_ty.get());
        return fut->element_type;
    }

    // await sem Future — aceitar como passthrough (para flexibilidade)
    return operand_ty ? operand_ty : checker->gettyptr("None");
}
