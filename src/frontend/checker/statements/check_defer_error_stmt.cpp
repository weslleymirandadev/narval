#include "frontend/checker/statements/check_defer_error_stmt.hpp"
#include "frontend/ast/statements/defer_error_stmt_node.hpp"

std::shared_ptr<nv::Type>& check_defer_error_stmt(nv::Checker* checker, Node* node) {
    auto* defer = static_cast<DeferErrorStmtNode*>(node);

    // Verificar o remaining_body com o escopo atual
    for (auto& stmt : defer->remaining_body)
        if (stmt) checker->check_node(stmt.get());

    // Verificar o handler com `err` vinculado ao tipo Error
    // or_block_depth > 0 permite o uso de `err` sem erro do checker
    checker->push_scope();
    checker->scope->put_key("err", checker->gettyptr("Error"), true);
    checker->or_block_depth++;
    for (auto& stmt : defer->handler_body)
        if (stmt) checker->check_node(stmt.get());
    checker->or_block_depth--;
    checker->pop_scope();

    return checker->gettyptr("None");
}
