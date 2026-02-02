#include "frontend/checker/statements/check_if_stmt.hpp"
#include "frontend/ast/statements/if_statement_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_if_stmt(nv::Checker* ch, Node* node) {
    auto* if_stmt = static_cast<IfStatementNode*>(node);
    
    // Verificar condição
    if (!if_stmt->condition) {
        ch->error(node, "If statement requires a condition");
        return ch->gettyptr("void");
    }
    
    auto cond_type = ch->infer_expr(if_stmt->condition.get());
    cond_type = ch->unify_ctx.resolve(cond_type);
    
    // Verificar que a condição é bool
    try {
        ch->unify_ctx.unify(cond_type, ch->gettyptr("bool"));
    } catch (std::runtime_error& e) {
        ch->error(if_stmt->condition.get(), 
                  "If condition must be of type 'bool', but got '" + cond_type->toString() + "'");
        return ch->gettyptr("void");
    }
    
    // Verificar bloco consequent
    ch->push_scope();
    for (auto& stmt : if_stmt->consequent) {
        ch->check_node(stmt.get());
    }
    ch->pop_scope();
    
    // Verificar bloco alternate (else)
    ch->push_scope();
    for (auto& stmt : if_stmt->alternate) {
        ch->check_node(stmt.get());
    }
    ch->pop_scope();
    
    return ch->gettyptr("void");
}
