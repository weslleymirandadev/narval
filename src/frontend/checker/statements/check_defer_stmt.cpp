#include "frontend/checker/statements/check_defer_stmt.hpp"
#include "frontend/ast/statements/defer_stmt_node.hpp"

std::shared_ptr<nv::Type>& check_defer_stmt(nv::Checker* checker, Node* node) {
    auto* defer = static_cast<DeferStmtNode*>(node);

    for (auto& stmt : defer->remaining_body)
        if (stmt) checker->check_node(stmt.get());

    checker->push_scope();
    for (auto& stmt : defer->defer_body)
        if (stmt) checker->check_node(stmt.get());
    checker->pop_scope();

    return checker->gettyptr("None");
}
