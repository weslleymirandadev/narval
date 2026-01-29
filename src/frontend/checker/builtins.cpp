#include "frontend/checker/builtins.hpp"
#include "frontend/checker/checker.hpp"
#include <vector>
#include <algorithm>

namespace rph {
    
    // Lista de funções builtin disponíveis
    const std::vector<BuiltinFunction> BUILTIN_FUNCTIONS = {
        // write: aceita 0 ou 1 argumento de qualquer tipo e retorna void (polimórfico)
        // Aceita varargs: 0 ou 1 argumento
        BuiltinFunction("write", {}, std::make_shared<Void>(), true, true, 0, 1),
        
        // read: aceita 0 ou 1 argumento (prompt opcional), retorna string
        BuiltinFunction("read", {}, std::make_shared<String>(), false, true, 0, 1),
    };
    
    // Variáveis globais builtin (não são funções, mas objetos especiais)
    void register_builtin_variables(Checker& checker) {
        // json: objeto especial para operações JSON
        // No runtime, json é tratado como um objeto especial (Value*)
        // No checker, registramos como um tipo genérico que pode ter métodos chamados nele
        // Usamos um tipo especial que permite chamadas de método (como json.load)
        // Por simplicidade, registramos como um tipo que pode ser usado em expressões
        // O codegen trata json especialmente, então não precisamos de tipo muito específico aqui
        auto json_type = checker.unify_ctx.new_type_var();
        checker.scope->put_key("json", json_type, true);
    }
    
    bool builtin_accepts_args(const BuiltinFunction& builtin, size_t arg_count) {
        if (builtin.accepts_varargs) {
            return arg_count >= builtin.min_args && 
                   (builtin.max_args == 0 || arg_count <= builtin.max_args);
        }
        return arg_count == builtin.param_types.size();
    }
    
    void register_builtins(Checker& checker) {
        // Registrar funções builtin
        for (const auto& builtin : BUILTIN_FUNCTIONS) {
            std::shared_ptr<Type> func_type;
            
            if (builtin.is_polymorphic) {
                // Criar tipo polimórfico
                // Para write: aceita 0 ou 1 argumento de qualquer tipo
                // Criamos uma variável de tipo para o parâmetro opcional
                auto param_type = checker.unify_ctx.new_type_var();
                std::vector<std::shared_ptr<Type>> param_types = {param_type};
                
                func_type = std::make_shared<Label>(param_types, builtin.return_type);
                
                // Generalizar tipo (criar tipo polimórfico)
                // Coletar variáveis livres (apenas a variável do parâmetro)
                std::unordered_set<int> free_vars;
                param_type->collect_free_vars(free_vars);
                func_type = checker.unify_ctx.generalize(func_type, free_vars);
            } else {
                // Tipo não polimórfico - usar tipos especificados
                func_type = std::make_shared<Label>(builtin.param_types, builtin.return_type);
            }
            
            // Registrar no escopo global como constante
            checker.scope->put_key(builtin.name, func_type, true);
        }
        
        // Registrar variáveis globais builtin
        register_builtin_variables(checker);
    }
}
