#include "frontend/checker/expressions/check_map_expr.hpp"
#include "frontend/ast/expressions/map_node.hpp"
#include "frontend/ast/expressions/key_value_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_map_expr(nv::Checker* ch, Node* node) {
    static thread_local std::shared_ptr<nv::Type> temp_result;
    auto* map_node = static_cast<MapNode*>(node);
    
    if (map_node->properties.empty()) {
        // Map vazio - usar tipos genéricos para key e value
        int next_key_id = ch->unify_ctx.get_next_var_id();
        int next_value_id = ch->unify_ctx.get_next_var_id();
        auto key_type = std::make_shared<nv::TypeVar>(next_key_id);
        auto value_type = std::make_shared<nv::TypeVar>(next_value_id);
        temp_result = std::make_shared<nv::Map>(key_type, value_type);
        return temp_result;
    }
    
    // Verificar que todas as propriedades são KeyValue
    std::shared_ptr<nv::Type> key_type = nullptr;
    std::shared_ptr<nv::Type> value_type = nullptr;
    
    for (auto& prop : map_node->properties) {
        if (prop->kind != NodeType::KeyValue) {
            ch->error(prop.get(), "Map properties must be key-value pairs");
            return ch->gettyptr("void");
        }
        
        auto* kv = static_cast<KeyValueNode*>(prop.get());
        
        if (!kv->key || !kv->value) {
            ch->error(prop.get(), "Key-value pair requires both key and value");
            return ch->gettyptr("void");
        }
        
        auto prop_key_type = ch->infer_expr(kv->key.get());
        auto prop_value_type = ch->infer_expr(kv->value.get());
        
        prop_key_type = ch->unify_ctx.resolve(prop_key_type);
        prop_value_type = ch->unify_ctx.resolve(prop_value_type);
        
        if (!key_type) {
            key_type = prop_key_type;
            value_type = prop_value_type;
        } else {
            // Unificar tipos de chave
            try {
                ch->unify_ctx.unify(key_type, prop_key_type);
            } catch (std::runtime_error& e) {
                ch->error(kv->key.get(), 
                          "Map key type mismatch: expected '" + key_type->toString() + 
                          "', but got '" + prop_key_type->toString() + "'");
                return ch->gettyptr("void");
            }
            
            // Unificar tipos de valor
            try {
                ch->unify_ctx.unify(value_type, prop_value_type);
            } catch (std::runtime_error& e) {
                ch->error(kv->value.get(), 
                          "Map value type mismatch: expected '" + value_type->toString() + 
                          "', but got '" + prop_value_type->toString() + "'");
                return ch->gettyptr("void");
            }
        }
    }
    
    // Resolver tipos finais
    key_type = ch->unify_ctx.resolve(key_type);
    value_type = ch->unify_ctx.resolve(value_type);
    
    // Criar e retornar tipo Map
    temp_result = std::make_shared<nv::Map>(key_type, value_type);
    return temp_result;
}
