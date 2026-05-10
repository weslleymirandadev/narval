#include "frontend/checker/statements/check_return_stmt.hpp"
#include "frontend/ast/statements/return_stmt_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_return_stmt(nv::Checker* ch, Node* node) {
    auto* return_stmt = static_cast<ReturnStmtNode*>(node);
    
    // `return` é permitido dentro de blocos `or { }` (redireciona o resultado do or)
    if (!ch->current_return_type && ch->or_block_depth == 0) {
        ch->error(node, "Return statement outside of function");
        return ch->gettyptr("void");
    }
    // Dentro de um bloco or sem função envolvente, aceitar qualquer tipo de retorno
    if (!ch->current_return_type) {
        if (return_stmt->value) ch->infer_expr(return_stmt->value.get());
        return ch->gettyptr("void");
    }
    
    // Verificar valor de retorno (se houver)
    if (return_stmt->value) {
        auto return_value_type = ch->infer_expr(return_stmt->value.get());
        return_value_type = ch->unify_ctx.resolve(return_value_type);
        
        // Resolver tipo de retorno esperado
        auto expected_return_type = ch->unify_ctx.resolve(ch->current_return_type);
        
        // Verificar coerção implícita int -> float
        bool return_is_int = return_value_type->kind == nv::Kind::INT;
        bool return_is_float = return_value_type->kind == nv::Kind::FLOAT;
        bool expected_is_int = expected_return_type->kind == nv::Kind::INT;
        bool expected_is_float = expected_return_type->kind == nv::Kind::FLOAT;
        
        // Se um é int e outro é float, promover int para float
        if (return_is_int && expected_is_float) {
            return_value_type = ch->gettyptr("float");
        } else if (return_is_float && expected_is_int) {
            expected_return_type = ch->gettyptr("float");
        }
        
        // Funções falíveis retornam Result<T> — não verificar correspondência exata do tipo.
        if (!ch->in_fallible_function) {
            try {
                ch->unify_ctx.unify(return_value_type, expected_return_type);
            } catch (std::runtime_error& e) {
                ch->error(return_stmt->value.get(),
                          "Return type mismatch: expected '" + expected_return_type->toString() +
                          "', but got '" + return_value_type->toString() + "'");
                return ch->gettyptr("void");
            }
        }
    } else {
        // Return sem valor - verificar que o tipo de retorno é void
        auto expected_return_type = ch->unify_ctx.resolve(ch->current_return_type);
        if (expected_return_type->kind != nv::Kind::VOID) {
            ch->error(node, "Return statement without value in function that returns '" + 
                      expected_return_type->toString() + "'");
            return ch->gettyptr("void");
        }
    }
    
    return ch->gettyptr("void");
}
