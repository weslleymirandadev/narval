#include "frontend/checker/expressions/check_access_expr.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_access_expr(nv::Checker* ch, Node* node) {
    static thread_local std::shared_ptr<nv::Type> temp_result;
    auto* access_expr = static_cast<AccessExprNode*>(node);
    
    if (!access_expr->expr) {
        ch->error(node, "Access expression requires an expression");
        return ch->gettyptr("void");
    }
    
    if (!access_expr->index) {
        ch->error(node, "Access expression requires an index");
        return ch->gettyptr("void");
    }
    
    // Verificar tipo da expressão base
    auto expr_type = ch->infer_expr(access_expr->expr.get());
    expr_type = ch->unify_ctx.resolve(expr_type);
    
    // Verificar tipo do índice
    auto index_type = ch->infer_expr(access_expr->index.get());
    index_type = ch->unify_ctx.resolve(index_type);
    
    // Verificar que o índice é int ou string (para Map)
    bool index_is_int = index_type->kind == nv::Kind::INT;
    bool index_is_string = index_type->kind == nv::Kind::STRING;
    
    if (!index_is_int && !index_is_string) {
        ch->error(access_expr->index.get(), 
                  "Access index must be int or string, but got '" + index_type->toString() + "'");
        return ch->gettyptr("void");
    }
    
    // Verificar que a expressão base suporta acesso
    if (expr_type->kind == nv::Kind::ARRAY) {
        // Array - índice deve ser int
        if (!index_is_int) {
            ch->error(access_expr->index.get(), 
                      "Array access requires integer index");
            return ch->gettyptr("void");
        }
        
        auto* arr = static_cast<nv::Array*>(expr_type.get());
        return arr->element_type;
    } else if (expr_type->kind == nv::Kind::VECTOR) {
        // Vector - índice deve ser int, retorna tipo genérico
        if (!index_is_int) {
            ch->error(access_expr->index.get(), 
                      "Vector access requires integer index");
            return ch->gettyptr("void");
        }
        
        // Vector pode conter qualquer tipo, retornar tipo genérico
        int next_id = ch->unify_ctx.get_next_var_id();
        temp_result = std::make_shared<nv::TypeVar>(next_id);
        return temp_result;
    } else if (expr_type->kind == nv::Kind::STRING) {
        // String - índice deve ser int, retorna string (caractere)
        if (!index_is_int) {
            ch->error(access_expr->index.get(), 
                      "String access requires integer index");
            return ch->gettyptr("void");
        }
        
        return ch->gettyptr("string");
    } else if (expr_type->kind == nv::Kind::MAP) {
        // Map - índice deve ser compatível com o tipo da chave
        auto* map = static_cast<nv::Map*>(expr_type.get());
        
        // Verificar compatibilidade do tipo do índice com o tipo da chave
        try {
            ch->unify_ctx.unify(index_type, map->key_type);
        } catch (std::runtime_error& e) {
            ch->error(access_expr->index.get(), 
                      "Map access index type '" + index_type->toString() + 
                      "' is not compatible with key type '" + map->key_type->toString() + "'");
            return ch->gettyptr("void");
        }
        
        return map->value_type;
    } else if (expr_type->kind == nv::Kind::TUPLE) {
        // Tuple - índice deve ser int
        if (!index_is_int) {
            ch->error(access_expr->index.get(), 
                      "Tuple access requires integer index");
            return ch->gettyptr("void");
        }
        
        auto* tuple = static_cast<nv::Tuple*>(expr_type.get());
        // TODO: verificar se o índice está dentro dos limites do tuple
        // Por enquanto, retornar tipo genérico
        int next_id = ch->unify_ctx.get_next_var_id();
        temp_result = std::make_shared<nv::TypeVar>(next_id);
        return temp_result;
    } else {
        ch->error(access_expr->expr.get(), 
                  "Access expression requires array, vector, string, map, or tuple, but got '" + 
                  expr_type->toString() + "'");
        return ch->gettyptr("void");
    }
}
