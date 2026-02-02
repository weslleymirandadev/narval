#include "frontend/checker/statements/check_while_stmt.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_while_stmt(nv::Checker* ch, Node* node) {
    auto* while_stmt = static_cast<WhileStmtNode*>(node);
    
    // Verificar condição
    if (!while_stmt->condition) {
        ch->error(node, "While statement requires a condition");
        return ch->gettyptr("void");
    }
    
    auto cond_type = ch->infer_expr(while_stmt->condition.get());
    cond_type = ch->unify_ctx.resolve(cond_type);
    
    // Verificar que a condição é bool
    try {
        ch->unify_ctx.unify(cond_type, ch->gettyptr("bool"));
    } catch (std::runtime_error& e) {
        ch->error(while_stmt->condition.get(), 
                  "While condition must be of type 'bool', but got '" + cond_type->toString() + "'");
        return ch->gettyptr("void");
    }
    
    // Verificar corpo do loop
    ch->push_scope();
    for (auto& stmt : while_stmt->body) {
        ch->check_node(stmt.get());
    }
    ch->pop_scope();
    
    return ch->gettyptr("void");
}
