#include "frontend/checker/expressions/check_access_expr.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>
#include "frontend/ast/expressions/tuple_expr_node.hpp"

std::shared_ptr<nv::Type>& check_access_expr(nv::Checker* ch, Node* node) {
    static thread_local std::shared_ptr<nv::Type> temp_result;
    auto* access_expr = static_cast<AccessExprNode*>(node);
    
    if (!access_expr->expr) {
        ch->error(node, "Access expression requires an expression");
        return ch->gettyptr("None");
    }
    
    if (!access_expr->index) {
        ch->error(node, "Access expression requires an index");
        return ch->gettyptr("None");
    }
    
    // Verificar tipo da expressão base
    auto expr_type = ch->infer_expr(access_expr->expr.get());
    expr_type = ch->unify_ctx.resolve(expr_type);
    
    // Verificar tipo do índice
    auto index_type = ch->infer_expr(access_expr->index.get());
    index_type = ch->unify_ctx.resolve(index_type);
    
    // Verificar que o índice é int ou string (para Map)
    bool index_is_int    = index_type->kind == nv::Kind::INT;
    bool index_is_string = index_type->kind == nv::Kind::STRING;

    // TypeVar não resolvida usada como índice → inferir como int (HM: constrangimento de uso)
    if (!index_is_int && !index_is_string && index_type->kind == nv::Kind::TYPE_VAR) {
        try {
            ch->unify_ctx.unify(index_type, ch->gettyptr("int"));
            index_is_int = true;
        } catch (...) {}
    }

    // A tensor addressed by several coordinates is the one case where the index is neither
    // an int nor a string: the tuple is checked below, against the tensor's rank.
    const bool tensor_coords = expr_type->kind == nv::Kind::TENSOR &&
                               access_expr->index &&
                               access_expr->index->kind == NodeType::TupleExpression;
    if (!index_is_int && !index_is_string && !tensor_coords) {
        ch->error(access_expr->index.get(),
                  "Access index must be int or string, but got '" + index_type->toString() + "'");
        return ch->gettyptr("None");
    }
    
    // Verificar que a expressão base suporta acesso
    if (expr_type->kind == nv::Kind::ARRAY) {
        // Array - índice deve ser int
        if (!index_is_int) {
            ch->error(access_expr->index.get(), 
                      "Array access requires integer index");
            return ch->gettyptr("None");
        }
        
        auto* arr = static_cast<nv::Array*>(expr_type.get());
        return arr->element_type;
    } else if (expr_type->kind == nv::Kind::VECTOR) {
        // Vector - índice deve ser int, retorna tipo genérico
        if (!index_is_int) {
            ch->error(access_expr->index.get(), 
                      "Vector access requires integer index");
            return ch->gettyptr("None");
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
            return ch->gettyptr("None");
        }
        
        return ch->gettyptr("str");
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
            return ch->gettyptr("None");
        }
        
        return map->value_type;
    } else if (expr_type->kind == nv::Kind::TUPLE) {
        // Tuple - índice deve ser int
        if (!index_is_int) {
            ch->error(access_expr->index.get(), 
                      "Tuple access requires integer index");
            return ch->gettyptr("None");
        }
        
        auto* tuple = static_cast<nv::Tuple*>(expr_type.get());
        // TODO: verificar se o índice está dentro dos limites do tuple
        // Por enquanto, retornar tipo genérico
        int next_id = ch->unify_ctx.get_next_var_id();
        temp_result = std::make_shared<nv::TypeVar>(next_id);
        return temp_result;
    } else if (expr_type->kind == nv::Kind::TYPE_VAR) {
        // The element of an untyped `vector` (or of a still-unknown value) is itself
        // an unknown type, and unknown values are indexable — this is what makes
        // `rows[i][j]` work. Constraining the type var to `vector` here would wrongly
        // narrow an inferred parameter, so just yield another unknown.
        if (!index_is_int && !index_is_string) {
            ch->error(access_expr->index.get(),
                      "Access index must be int or string, but got '" + index_type->toString() + "'");
            return ch->gettyptr("None");
        }
        int next_id = ch->unify_ctx.get_next_var_id();
        temp_result = std::make_shared<nv::TypeVar>(next_id);
        return temp_result;
    } else if (expr_type->kind == nv::Kind::FUNCTION && index_is_int) {
        // Closure arrays currently flow through annotations like |x:int|:int[2].
        // Treat indexing such a value as retrieving a callable element.
        temp_result = expr_type;
        return temp_result;
    } else if (expr_type->kind == nv::Kind::TENSOR && access_expr->index &&
               access_expr->index->kind == NodeType::TupleExpression) {
        // Several coordinates into a tensor: one per dimension, and the shape is checked
        // here because the checker is the only place that knows it. The offsets themselves
        // come from the tensor's own shape at runtime, so dynamic dimensions are fine.
        auto* tup = static_cast<TupleExprNode*>(access_expr->index.get());
        auto* tensor_type = static_cast<nv::TensorType*>(expr_type.get());
        if (tup->elements.size() != tensor_type->dims.size()) {
            ch->error(access_expr->index.get(),
                      "Tensor has " + std::to_string(tensor_type->dims.size()) +
                      " dimension(s), but " + std::to_string(tup->elements.size()) +
                      " index(es) were given.");
            return ch->gettyptr("None");
        }
        for (auto& el : tup->elements) {
            if (!el) continue;
            auto et = ch->unify_ctx.resolve(ch->infer_expr(el.get()));
            if (et && et->kind != nv::Kind::INT && et->kind != nv::Kind::FLOAT) {
                ch->error(el.get(), "Tensor index must be an integer, but got '" +
                                    et->toString() + "'.");
                return ch->gettyptr("None");
            }
        }
        access_expr->tensor_multi_index = true;
        temp_result = tensor_type->element;
        return temp_result;
    } else if (expr_type->kind == nv::Kind::TENSOR && index_is_int) {
        // Flat element access on a tensor: its numbers are contiguous, so `t[i]` is the
        // i-th element. This is what makes an element-wise loop over tensors expressible
        // at all (`c[i] = a[i] + b[i]`), which is exactly the shape @[vectorize] lowers to
        // raw loads and stores. Float tensors (the type has a float element).
        temp_result = ch->gettyptr("float");
        return temp_result;
    } else {
        ch->error(access_expr->expr.get(), 
                  "Access expression requires array, vector, string, map, or tuple, but got '" + 
                  expr_type->toString() + "'");
        return ch->gettyptr("None");
    }
}
