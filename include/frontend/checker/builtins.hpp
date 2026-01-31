#pragma once
#include "frontend/checker/type.hpp"
#include "frontend/checker/unification.hpp"
#include <vector>
#include <memory>
#include <string>

namespace nv {
    // Forward declaration
    class Checker;
    /**
     * Estrutura para definir uma função builtin
     */
    struct BuiltinFunction {
        std::string name;
        std::vector<std::shared_ptr<Type>> param_types;  // Tipos dos parâmetros obrigatórios
        std::shared_ptr<Type> return_type;
        bool is_polymorphic;  // Se true, cria tipo polimórfico
        bool accepts_varargs;  // Se true, aceita número variável de argumentos (0 ou mais)
        size_t min_args;  // Número mínimo de argumentos
        size_t max_args;  // Número máximo de argumentos (0 = ilimitado)
        
        BuiltinFunction(const std::string& n, 
                       const std::vector<std::shared_ptr<Type>>& params,
                       std::shared_ptr<Type> ret,
                       bool poly = false,
                       bool varargs = false,
                       size_t min = 0,
                       size_t max = 0)
            : name(n), param_types(params), return_type(ret), 
              is_polymorphic(poly), accepts_varargs(varargs),
              min_args(min), max_args(max) {}
    };
    
    /**
     * Registra funções builtin no checker
     */
    void register_builtins(Checker& checker);
    
    /**
     * Verifica se uma função builtin aceita um número específico de argumentos
     */
    bool builtin_accepts_args(const BuiltinFunction& builtin, size_t arg_count);
    
    /**
     * Lista de funções builtin disponíveis
     */
    extern const std::vector<BuiltinFunction> BUILTIN_FUNCTIONS;
}
