#include "frontend/checker/statements/check_def_stmt.hpp"
#include "frontend/ast/statements/def_stmt_node.hpp"
#include "frontend/ast/expressions/param_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

std::shared_ptr<nv::Type>& check_def_stmt(nv::Checker* ch, Node* node) {
    auto* def_stmt = static_cast<DefStmtNode*>(node);
    
    // Criar novo escopo para a função
    ch->push_scope();
    
    // Processar parâmetros e adicionar ao escopo
    std::vector<std::shared_ptr<nv::Type>> param_types;
    for (const auto& param : def_stmt->parameters) {
        // ParamNode contém um unordered_map onde a chave é o nome do parâmetro e o valor é o tipo
        // O parser cria: param[nome] = tipo
        std::string param_name;
        std::string param_type_str;
        
        // O map tem apenas uma entrada: nome -> tipo
        if (param.parameter.size() != 1) {
            ch->error(node, "Invalid parameter format");
            ch->pop_scope();
            return ch->gettyptr("void");
        }
        
        // Extrair nome e tipo do map
        for (const auto& [key, value] : param.parameter) {
            param_name = key;  // A chave é o nome do parâmetro
            param_type_str = value;  // O valor é o tipo
        }
        
        if (param_name.empty()) {
            ch->error(node, "Parameter name is required");
            ch->pop_scope();
            return ch->gettyptr("void");
        }
        
        // Obter tipo do parâmetro
        std::shared_ptr<nv::Type> param_type;
        if (param_type_str.empty() || param_type_str == "automatic") {
            // Tipo automático - criar variável de tipo
            int next_id = ch->unify_ctx.get_next_var_id();
            param_type = std::make_shared<nv::TypeVar>(next_id);
        } else {
            param_type = ch->gettyptr(param_type_str);
        }
        
        // Adicionar parâmetro ao escopo
        ch->scope->put_key(param_name, param_type, false);
        param_types.push_back(param_type);
    }
    
    // Verificar tipo de retorno
    std::shared_ptr<nv::Type> return_type;
    if (def_stmt->return_type.empty() || def_stmt->return_type == "automatic") {
        // Inferir tipo de retorno do corpo da função
        // Por enquanto, usar void como padrão se não houver return
        return_type = ch->gettyptr("void");
    } else {
        return_type = ch->gettyptr(def_stmt->return_type);
    }
    
    // Salvar tipo de retorno atual e restaurar após verificar corpo
    auto saved_return_type = ch->current_return_type;
    ch->current_return_type = return_type;
    
    // Verificar corpo da função
    for (auto& stmt : def_stmt->body) {
        ch->check_node(stmt.get());
    }
    
    // Restaurar tipo de retorno anterior
    ch->current_return_type = saved_return_type;
    
    // Criar tipo de função e registrar no escopo pai
    ch->pop_scope();
    
    // Criar função type (PolyType para suportar polimorfismo)
    auto free_vars = ch->get_free_vars_in_env();
    auto generalized_return = ch->unify_ctx.generalize(return_type, free_vars);
    
    // Criar tipo de função (usar Def ao invés de Function)
    auto func_type = std::make_shared<nv::Def>(param_types, generalized_return);
    
    // Generalizar tipo de função
    auto generalized_func = ch->unify_ctx.generalize(func_type, free_vars);
    
    // Registrar função no escopo
    ch->scope->put_key(def_stmt->name, generalized_func, false);
    
    return ch->scope->get_key(def_stmt->name);
}
