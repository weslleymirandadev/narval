#include "frontend/checker/statements/check_for_stmt.hpp"
#include "frontend/ast/statements/for_stmt_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_for_stmt(nv::Checker* ch, Node* node) {
    auto* for_stmt = static_cast<ForStmtNode*>(node);
    
    // Criar novo escopo para o loop
    ch->push_scope();
    
    // Verificar se temos bindings ou iterable
    if (!for_stmt->bindings.empty()) {
        // Modo 1: for binding: range/iterable
        // Verificar range ou iterable
        if (for_stmt->range_start && for_stmt->range_end) {
            // Range mode
            auto start_type = ch->infer_expr(for_stmt->range_start.get());
            auto end_type = ch->infer_expr(for_stmt->range_end.get());
            
            start_type = ch->unify_ctx.resolve(start_type);
            end_type = ch->unify_ctx.resolve(end_type);
            
            // Verificar que start e end são compatíveis
            try {
                ch->unify_ctx.unify(start_type, end_type);
            } catch (std::runtime_error& e) {
                ch->error(for_stmt->range_start.get(), 
                          "Range start and end types must be compatible");
                ch->pop_scope();
                return ch->gettyptr("void");
            }
            
            // Verificar que são tipos iteráveis (int ou string)
            bool start_is_int = start_type->kind == nv::Kind::INT;
            bool start_is_string = start_type->kind == nv::Kind::STRING;
            
            if (!start_is_int && !start_is_string) {
                ch->error(for_stmt->range_start.get(), 
                          "Range bounds must be integers or strings");
                ch->pop_scope();
                return ch->gettyptr("void");
            }
            
            // Adicionar bindings ao escopo com tipo inferido do range
            for (auto& binding : for_stmt->bindings) {
                if (binding->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(binding.get());
                    ch->scope->put_key(id->symbol, start_type, false);
                }
            }
        } else if (for_stmt->iterable) {
            // Iterable mode
            auto iterable_type = ch->infer_expr(for_stmt->iterable.get());
            iterable_type = ch->unify_ctx.resolve(iterable_type);
            
            // Verificar que é um tipo iterável (Array, Vector, String, Map, Tuple)
            bool is_iterable = iterable_type->kind == nv::Kind::ARRAY ||
                              iterable_type->kind == nv::Kind::VECTOR ||
                              iterable_type->kind == nv::Kind::STRING ||
                              iterable_type->kind == nv::Kind::MAP ||
                              iterable_type->kind == nv::Kind::TUPLE;
            
            if (!is_iterable) {
                ch->error(for_stmt->iterable.get(), 
                          "For loop iterable must be an array, vector, string, map, or tuple");
                ch->pop_scope();
                return ch->gettyptr("void");
            }
            
            // Inferir tipo dos elementos do iterable
            std::shared_ptr<nv::Type> element_type;
            if (iterable_type->kind == nv::Kind::ARRAY) {
                auto* arr = static_cast<nv::Array*>(iterable_type.get());
                element_type = arr->element_type;
            } else if (iterable_type->kind == nv::Kind::VECTOR) {
                // Vector pode ter elementos heterogêneos, usar tipo genérico
                int next_id = ch->unify_ctx.get_next_var_id();
                element_type = std::make_shared<nv::TypeVar>(next_id);
            } else if (iterable_type->kind == nv::Kind::STRING) {
                element_type = ch->gettyptr("string");
            } else if (iterable_type->kind == nv::Kind::MAP) {
                // Para Map, o binding recebe uma tupla (key, value)
                auto* map = static_cast<nv::Map*>(iterable_type.get());
                std::vector<std::shared_ptr<nv::Type>> tuple_types = {
                    map->key_type,
                    map->value_type
                };
                element_type = std::make_shared<nv::Tuple>(tuple_types);
            } else if (iterable_type->kind == nv::Kind::TUPLE) {
                // Para Tuple, usar tipo genérico já que pode ter múltiplos elementos
                int next_id = ch->unify_ctx.get_next_var_id();
                element_type = std::make_shared<nv::TypeVar>(next_id);
            }
            
            // Adicionar bindings ao escopo
            for (auto& binding : for_stmt->bindings) {
                if (binding->kind == NodeType::Identifier) {
                    auto* id = static_cast<IdentifierNode*>(binding.get());
                    ch->scope->put_key(id->symbol, element_type, false);
                }
            }
        } else {
            ch->error(node, "For loop requires either a range or an iterable");
            ch->pop_scope();
            return ch->gettyptr("void");
        }
    } else if (for_stmt->iterable) {
        // Modo 2: for iterable (sem bindings explícitos)
        auto iterable_type = ch->infer_expr(for_stmt->iterable.get());
        iterable_type = ch->unify_ctx.resolve(iterable_type);
        
        // Verificar que é um tipo iterável
        bool is_iterable = iterable_type->kind == nv::Kind::ARRAY ||
                          iterable_type->kind == nv::Kind::VECTOR ||
                          iterable_type->kind == nv::Kind::STRING ||
                          iterable_type->kind == nv::Kind::MAP ||
                          iterable_type->kind == nv::Kind::TUPLE;
        
        if (!is_iterable) {
            ch->error(for_stmt->iterable.get(), 
                      "For loop iterable must be an array, vector, string, map, or tuple");
            ch->pop_scope();
            return ch->gettyptr("void");
        }
    } else {
        ch->error(node, "For loop requires bindings or an iterable");
        ch->pop_scope();
        return ch->gettyptr("void");
    }
    
    // Verificar corpo do loop
    for (auto& stmt : for_stmt->body) {
        ch->check_node(stmt.get());
    }
    
    // Verificar bloco else (se existir)
    for (auto& stmt : for_stmt->else_block) {
        ch->check_node(stmt.get());
    }
    
    ch->pop_scope();
    return ch->gettyptr("void");
}
