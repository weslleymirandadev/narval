#include "frontend/checker/expressions/check_list_comp_expr.hpp"
#include "frontend/ast/expressions/list_comp_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_list_comp_expr(nv::Checker* ch, Node* node) {
    auto* list_comp = static_cast<ListCompNode*>(node);
    
    if (!list_comp->elt) {
        ch->error(node, "List comprehension requires an element expression");
        return ch->gettyptr("void");
    }
    
    if (list_comp->generators.empty()) {
        ch->error(node, "List comprehension requires at least one generator");
        return ch->gettyptr("void");
    }
    
    // Criar novo escopo para os geradores
    ch->push_scope();
    
    // Verificar cada gerador
    for (const auto& [target, source] : list_comp->generators) {
        if (!target || !source) {
            ch->error(node, "Generator requires both target and source");
            ch->pop_scope();
            return ch->gettyptr("void");
        }
        
        // Verificar tipo da fonte (deve ser iterável)
        auto source_type = ch->infer_expr(source.get());
        source_type = ch->unify_ctx.resolve(source_type);
        
        bool is_iterable = source_type->kind == nv::Kind::ARRAY ||
                          source_type->kind == nv::Kind::VECTOR ||
                          source_type->kind == nv::Kind::STRING ||
                          source_type->kind == nv::Kind::MAP ||
                          source_type->kind == nv::Kind::TUPLE;
        
        if (!is_iterable) {
            ch->error(source.get(), 
                      "List comprehension source must be iterable (array, vector, string, map, or tuple)");
            ch->pop_scope();
            return ch->gettyptr("void");
        }
        
        // Inferir tipo dos elementos
        std::shared_ptr<nv::Type> element_type;
        if (source_type->kind == nv::Kind::ARRAY) {
            auto* arr = static_cast<nv::Array*>(source_type.get());
            element_type = arr->element_type;
        } else if (source_type->kind == nv::Kind::VECTOR) {
            int next_id = ch->unify_ctx.get_next_var_id();
            element_type = std::make_shared<nv::TypeVar>(next_id);
        } else if (source_type->kind == nv::Kind::STRING) {
            element_type = ch->gettyptr("string");
        } else if (source_type->kind == nv::Kind::MAP) {
            // Para Map, o elemento é uma tupla (key, value)
            auto* map = static_cast<nv::Map*>(source_type.get());
            std::vector<std::shared_ptr<nv::Type>> tuple_types = {
                map->key_type,
                map->value_type
            };
            element_type = std::make_shared<nv::Tuple>(tuple_types);
        } else {
            // Tuple - usar tipo genérico
            int next_id = ch->unify_ctx.get_next_var_id();
            element_type = std::make_shared<nv::TypeVar>(next_id);
        }
        
        // Adicionar target ao escopo
        if (target->kind == NodeType::Identifier) {
            auto* id = static_cast<IdentifierNode*>(target.get());
            ch->scope->put_key(id->symbol, element_type, false);
        }
    }
    
    // Verificar condição (se houver)
    if (list_comp->if_cond) {
        auto cond_type = ch->infer_expr(list_comp->if_cond.get());
        cond_type = ch->unify_ctx.resolve(cond_type);
        
        try {
            ch->unify_ctx.unify(cond_type, ch->gettyptr("bool"));
        } catch (std::runtime_error& e) {
            ch->error(list_comp->if_cond.get(), 
                      "List comprehension condition must be of type 'bool'");
            ch->pop_scope();
            return ch->gettyptr("void");
        }
    }
    
    // Verificar expressão do elemento
    auto elt_type = ch->infer_expr(list_comp->elt.get());
    elt_type = ch->unify_ctx.resolve(elt_type);
    
    // Verificar else_expr (se houver)
    if (list_comp->else_expr) {
        auto else_type = ch->infer_expr(list_comp->else_expr.get());
        else_type = ch->unify_ctx.resolve(else_type);
        
        // Unificar tipos
        try {
            ch->unify_ctx.unify(elt_type, else_type);
        } catch (std::runtime_error& e) {
            ch->error(list_comp->else_expr.get(), 
                      "List comprehension element and else expression have incompatible types");
            ch->pop_scope();
            return ch->gettyptr("void");
        }
    }
    
    ch->pop_scope();
    
    // Retornar Vector (list comprehension sempre retorna vector)
    static thread_local std::shared_ptr<nv::Type> temp_result;
    temp_result = std::make_shared<nv::Vector>();
    return temp_result;
}
