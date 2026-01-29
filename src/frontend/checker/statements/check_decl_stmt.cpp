#include "frontend/checker/statements/check_decl_stmt.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/ast/ast.hpp"
#include <stdexcept>

std::shared_ptr<rph::Type>& check_decl_stmt(rph::Checker* ch, Node* node) {
    auto* decl = static_cast<DeclarationStmtNode*>(node);
    auto* name = static_cast<IdentifierNode*>(decl->target.get());
    
    if (decl->typ == "automatic") {
        // Usar inferência de tipos
        auto inferred_type = ch->infer_expr(decl->value.get());
        
        // Resolver tipo após inferência
        inferred_type = ch->unify_ctx.resolve(inferred_type);
        
        // Generalizar tipo (criar tipo polimórfico se necessário)
        auto free_in_env = ch->get_free_vars_in_env();
        auto generalized = ch->unify_ctx.generalize(inferred_type, free_in_env);
        
        ch->scope->put_key(name->symbol, generalized, decl->constant);
        
        // Retornar referência ao tipo no namespace
        return ch->scope->get_key(name->symbol);
    } else {
        // Tipo explícito - usar verificação tradicional com unificação
        auto& dtype = ch->gettyptr(decl->typ);
        auto vtype = ch->infer_expr(decl->value.get());
        
        // Unificar tipo inferido com tipo declarado
        try {
            ch->unify_ctx.unify(vtype, dtype);
        } catch (std::runtime_error& e) {
            throw std::runtime_error(std::string("Expected type '") + dtype->toString() + 
                                   "', but got '" + vtype->toString() + "'. " + e.what());
        }
        
        // Resolver tipo após unificação
        dtype = ch->unify_ctx.resolve(dtype);
        ch->scope->put_key(name->symbol, dtype, decl->constant);
        return ch->scope->get_key(name->symbol);
    }
}
