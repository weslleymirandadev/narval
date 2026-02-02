#include "frontend/checker/statements/check_loop_stmt.hpp"
#include "frontend/ast/statements/loop_stmt_node.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_loop_stmt(nv::Checker* ch, Node* node) {
    auto* loop_stmt = static_cast<LoopStmtNode*>(node);
    
    // Loop infinito - apenas verificar corpo
    ch->push_scope();
    for (auto& stmt : loop_stmt->body) {
        ch->check_node(stmt.get());
    }
    ch->pop_scope();
    
    return ch->gettyptr("void");
}
