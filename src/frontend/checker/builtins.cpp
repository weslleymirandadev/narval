#include "frontend/checker/builtins.hpp"
#include "frontend/checker/checker.hpp"
#include <vector>
#include <algorithm>

namespace nv {
    
    // Lista de funções builtin disponíveis
    const std::vector<BuiltinFunction> BUILTIN_FUNCTIONS = {
        // write: aceita 1 argumento de qualquer tipo -> void (como print() do Python)
        BuiltinFunction("write", {}, std::make_shared<Void>(), false, true, 1, 1),
        
        // read: aceita 0 ou 1 argumento (prompt opcional), retorna string
        BuiltinFunction("read", {}, std::make_shared<String>(), false, true, 0, 1),
        
        // exit: encerra o processo com o código dado (int) -> void
        BuiltinFunction("exit", {std::make_shared<Int>()}, std::make_shared<Void>(), false, false, 1, 1),
        
        // Funções de conversão de tipo (estilo Python)
        // str: aceita 1 argumento de qualquer tipo, retorna string
        BuiltinFunction("str", {}, std::make_shared<String>(), false, true, 1, 1),
        
        // int: aceita 1 argumento de qualquer tipo, retorna int
        BuiltinFunction("int", {}, std::make_shared<Int>(), false, true, 1, 1),
        
        // float: aceita 1 argumento de qualquer tipo, retorna float
        BuiltinFunction("float", {}, std::make_shared<Float>(), false, true, 1, 1),
        
        // bool: aceita 1 argumento de qualquer tipo, retorna bool
        BuiltinFunction("bool", {}, std::make_shared<Boolean>(), false, true, 1, 1),
    };
    
    // Variáveis globais builtin (não são funções, mas objetos especiais)
    void register_builtin_variables(Checker& checker) {
        // json: objeto especial para operações JSON
        // Criamos um tipo específico para json com método parse
        // Usamos Map como tipo genérico para JSON objetos, Vector para arrays
        auto json_type = std::make_shared<Map>(checker.gettyptr("str"), checker.gettyptr("str"));
        json_type->init_prototype();  // Inicializar o prototype
        
        // Adicionar métodos manualmente ao prototype do json
        if (json_type->prototype) {
            // Método parse: (string) -> Map (para arquivo)
            auto parse_func_type = std::make_shared<Def>(
                std::vector<std::shared_ptr<Type>>{checker.gettyptr("str")},
                json_type
            );
            json_type->prototype->put_key("parse", parse_func_type, true);
            
            // Método parseString: (string) -> Map (para string JSON)
            auto parse_string_func_type = std::make_shared<Def>(
                std::vector<std::shared_ptr<Type>>{checker.gettyptr("str")},
                json_type
            );
            json_type->prototype->put_key("parseString", parse_string_func_type, true);
            
            // Método dump: (Map, string) -> void
            auto dump_func_type = std::make_shared<Def>(
                std::vector<std::shared_ptr<Type>>{json_type, checker.gettyptr("str")},
                checker.gettyptr("void")
            );
            json_type->prototype->put_key("dump", dump_func_type, true);
            
            // Método stringify: (Map) -> string
            auto stringify_func_type = std::make_shared<Def>(
                std::vector<std::shared_ptr<Type>>{json_type},
                checker.gettyptr("str")
            );
            json_type->prototype->put_key("stringify", stringify_func_type, true);
        }
        
        // Registrar o tipo json no scope
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
                
                func_type = std::make_shared<Def>(param_types, builtin.return_type);
                
                // Generalizar tipo (criar tipo polimórfico)
                // Coletar variáveis livres (apenas a variável do parâmetro)
                std::unordered_set<int> free_vars;
                param_type->collect_free_vars(free_vars);
                func_type = checker.unify_ctx.generalize(func_type, free_vars);
            } else {
                // Tipo não polimórfico - usar tipos especificados
                func_type = std::make_shared<Def>(builtin.param_types, builtin.return_type);
            }
            
            // Registrar no escopo global como constante
            checker.scope->put_key(builtin.name, func_type, true);
        }
        
        // Registrar variáveis globais builtin
        register_builtin_variables(checker);
    }
}
