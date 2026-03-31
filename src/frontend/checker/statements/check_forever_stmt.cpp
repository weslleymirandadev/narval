#include "frontend/checker/statements/check_forever_stmt.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_forever_stmt(nv::Checker* ch, Node* node) {
    auto* forever_stmt = static_cast<ForeverStmtNode*>(node);
    
    // Loop infinito - apenas verificar corpo
    ch->push_scope();
    for (auto& stmt : forever_stmt->body) {
        ch->check_node(stmt.get());
    }
    ch->pop_scope();
    
    return ch->gettyptr("void");
}
