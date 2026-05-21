#include "frontend/checker/statements/check_function_stmt.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "frontend/ast/expressions/param_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/or_expr_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/checker/unification.hpp"
#include <stdexcept>

// Verifica se um node (expr ou stmt) contém `propagate` em qualquer profundidade.
static bool node_has_propagate(const Node* node) {
    if (!node) return false;
    if (node->kind == NodeType::PropagateStatement) return true;
    if (node->kind == NodeType::OrExpression) {
        auto* or_node = static_cast<const OrExprNode*>(node);
        if (or_node->is_block_handler) {
            for (auto& s : or_node->block_stmts)
                if (node_has_propagate(s.get())) return true;
        }
        return node_has_propagate(or_node->expr.get());
    }
    if (node->kind == NodeType::AssignmentExpression) {
        auto* assign = static_cast<const AssignmentExprNode*>(node);
        return node_has_propagate(assign->value.get());
    }
    if (node->kind == NodeType::DeclarationStatement) {
        auto* decl = static_cast<const DeclarationStmtNode*>(node);
        return node_has_propagate(decl->value.get());
    }
    return false;
}

// Verifica se um bloco de statements contém `propagate` em qualquer profundidade.
static bool stmts_have_propagate(const std::vector<std::unique_ptr<Stmt>>& stmts) {
    for (auto& stmt : stmts)
        if (node_has_propagate(stmt.get())) return true;
    return false;
}

std::shared_ptr<nv::Type>& check_function_stmt(nv::Checker* ch, Node* node) {
    auto* function_stmt = static_cast<FunctionStmtNode*>(node);

    // Registrar parâmetros de tipo genérico como TypeVars no mapa de tipos.
    // Salvar valores anteriores para restaurar ao fim — evita que nomes como T, A, B
    // vazem para o escopo global e sejam aceitos em closures não-genéricas.
    std::vector<std::pair<std::string, std::shared_ptr<nv::Type>>> saved_type_params;
    for (const auto& tp_name : function_stmt->type_params) {
        auto prev_it = ch->types.find(tp_name);
        saved_type_params.push_back({tp_name,
            prev_it != ch->types.end() ? prev_it->second : nullptr});
        ch->types[tp_name] = ch->unify_ctx.new_type_var();
    }

    // Criar novo escopo para a função
    ch->push_scope();
    
    // Processar parâmetros e adicionar ao escopo
    std::vector<std::shared_ptr<nv::Type>> param_types;
    for (const auto& param : function_stmt->parameters) {
        // ParamNode contém um unordered_map onde a chave é o nome do parâmetro e o valor é o tipo
        // O parser cria: param[nome] = tipo
        std::string param_name;
        std::string param_type_str;
        
        // O map tem apenas uma entrada: nome -> tipo
        if (param.parameter.size() != 1) {
            ch->error(node, "Invalid parameter format");
            ch->pop_scope();
            return ch->gettyptr("None");
        }
        
        // Extrair nome e tipo do map
        for (const auto& [key, value] : param.parameter) {
            param_name = key;  // A chave é o nome do parâmetro
            param_type_str = value;  // O valor é o tipo
        }
        
        if (param_name.empty()) {
            ch->error(node, "Parameter name is required");
            ch->pop_scope();
            return ch->gettyptr("None");
        }
        
        // Obter tipo do parâmetro
        std::shared_ptr<nv::Type> param_type;
        if (param_type_str.empty() || param_type_str == "automatic") {
            // Tipo automático - criar variável de tipo
            int next_id = ch->unify_ctx.get_next_var_id();
            param_type = std::make_shared<nv::TypeVar>(next_id);
        } else {
            param_type = ch->gettyptr(param_type_str, node);
        }
        
        // Adicionar parâmetro ao escopo da função
        ch->scope->put_key(param_name, param_type, false);
        param_types.push_back(param_type);
    }
    
    // Verificar tipo de retorno
    std::shared_ptr<nv::Type> return_type;
    if (function_stmt->return_type.empty() || function_stmt->return_type == "automatic") {
        // Inferir tipo de retorno do corpo da função
        // Por enquanto, usar void como padrão se não houver return
        return_type = ch->gettyptr("None");
    } else {
        return_type = ch->gettyptr(function_stmt->return_type, node);
    }

    auto body_return_type = return_type;
    auto exposed_return_type = return_type;
    if (function_stmt->is_async) {
        if (return_type && return_type->kind == nv::Kind::FUTURE) {
            auto future = std::static_pointer_cast<nv::Future>(return_type);
            body_return_type = future->element_type;
            exposed_return_type = return_type;
        } else {
            exposed_return_type = std::make_shared<nv::Future>(return_type);
        }
    }
    
    // Inferência de fallibilidade: detectar propagate antes de checar o corpo.
    // Quando falível, o checker não força correspondência do tipo de retorno declarado
    // (o codegen emite Ok(value) automaticamente, tornando o tipo real Result<T>).
    bool is_fallible = stmts_have_propagate(function_stmt->body);
    if (is_fallible) function_stmt->is_fallible = true;

    // Salvar tipo de retorno atual e restaurar após verificar corpo
    auto saved_return_type = ch->current_return_type;
    bool saved_fallible = ch->in_fallible_function;
    ch->current_return_type = body_return_type;
    ch->in_fallible_function = is_fallible;

    
    // Implementar conversão local usando o mesmo algoritmo do process_codeblock
    for (size_t i = 0; i < function_stmt->body.size(); i++) {
        auto& stmt = function_stmt->body[i];
        
        // Verificar se é AssignmentExpression que precisa ser convertido
        if (stmt->kind == NodeType::AssignmentExpression) {
            auto* assign_node = static_cast<AssignmentExprNode*>(stmt.get());
            
            // Apenas converter assignments simples (operador =) com target Identifier
            if (assign_node->op == "=" && assign_node->target->kind == NodeType::Identifier) {
                auto* id_node = static_cast<IdentifierNode*>(assign_node->target.get());
                
                // Verificar se o identifier já existe no escopo atual
                // Se não existe, converter para declaração
                bool identifier_exists = false;
                try {
                    ch->scope->get_key(id_node->symbol);
                    identifier_exists = true;
                } catch (std::runtime_error&) {
                    identifier_exists = false;
                }
                
                if (!identifier_exists) {
                    // Converter AssignmentExpression para DeclarationStmtNode
                    auto new_target = std::unique_ptr<Expr>(static_cast<Expr*>(id_node->clone()));
                    auto new_value = assign_node->value ? 
                        std::unique_ptr<Expr>(static_cast<Expr*>(assign_node->value->clone())) : nullptr;
                    
                    auto decl_node = std::make_unique<DeclarationStmtNode>(
                        std::move(new_target),
                        std::move(new_value),
                        "automatic",  // tipo automático para inferência
                        false         // não constante (mutável)
                    );
                    
                    // Copiar posição do assignment para a declaração
                    if (assign_node->position) {
                        decl_node->position = std::make_unique<PositionData>(*assign_node->position);
                    }
                    
                    // Substituir o assignment pela declaração
                    stmt = std::move(decl_node);
                }
            }
        }
    }
    
    // Verificar o corpo processado (agora com declarações corretas)
    for (auto& stmt : function_stmt->body) {
        ch->check_node(stmt.get());
    }

    // Restaurar tipo de retorno anterior
    ch->current_return_type = saved_return_type;
    ch->in_fallible_function = saved_fallible;
    
    // Criar tipo de função e registrar no escopo pai
    
    // Criar função type (PolyType para suportar polimorfismo)
    auto free_vars = ch->get_free_vars_in_env();
    auto generalized_return = ch->unify_ctx.generalize(exposed_return_type, free_vars);
    
    auto func_type = std::make_shared<nv::Function>(param_types, generalized_return);
    
    // Generalizar tipo de função
    auto generalized_func = ch->unify_ctx.generalize(func_type, free_vars);
    
    std::vector<std::string> pname_list;
    std::vector<bool> param_has_default;
    size_t param_idx = 0;
    for (const auto& param : function_stmt->parameters) {
        if (!param.parameter.empty()) {
            for (const auto& kv : param.parameter) {
                pname_list.push_back(kv.first);
            }
        }
        param_has_default.push_back(param.default_value != nullptr);

        // Check default value type if present
        if (param.default_value && param_idx < param_types.size()) {
            auto default_type = ch->infer_expr(param.default_value.get());
            try {
                ch->unify_ctx.unify(default_type, param_types[param_idx]);
            } catch (std::runtime_error& e) {
                ch->error(param.default_value.get(),
                         "Default value type does not match parameter type: " + std::string(e.what()));
            }
        }
        param_idx++;
    }
    if (!pname_list.empty()) {
        ch->function_param_names[function_stmt->name] = pname_list;
        ch->function_param_defaults[function_stmt->name] = param_has_default;
        
        // Store default value expressions for codegen
        std::vector<std::unique_ptr<Expr>> default_values;
        for (const auto& param : function_stmt->parameters) {
            if (param.default_value) {
                default_values.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(param.default_value->clone())));
            } else {
                default_values.push_back(nullptr);
            }
        }
        ch->function_default_values[function_stmt->name] = std::move(default_values);
    }
    
    ch->pop_scope();
    ch->scope->put_key(function_stmt->name, generalized_func, false);

    // Restaurar type params no mapa global (remove os que não existiam antes)
    for (const auto& [name, prev] : saved_type_params) {
        if (prev) ch->types[name] = prev;
        else      ch->types.erase(name);
    }

    return ch->scope->get_key(function_stmt->name);
}
