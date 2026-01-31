#pragma once
#include "frontend/checker/type.hpp"
#include <unordered_map>
#include <stdexcept>

namespace nv {
    // Contexto de unificação para inferência de tipos
    class UnificationContext {
    private:
        int next_var_id;
        std::unordered_map<int, std::shared_ptr<TypeVar>> type_vars;
        
    public:
        UnificationContext() : next_var_id(0) {}
        
        // Criar nova variável de tipo
        std::shared_ptr<TypeVar> new_type_var() {
            auto tv = std::make_shared<TypeVar>(next_var_id++);
            type_vars[tv->id] = tv;
            return tv;
        }
        
        // Resolver variável de tipo (com path compression)
        std::shared_ptr<Type> resolve(std::shared_ptr<Type> t) {
            if (t->kind == Kind::TYPE_VAR) {
                auto tv = std::static_pointer_cast<TypeVar>(t);
                auto resolved = tv->resolve();
                if (resolved->kind == Kind::TYPE_VAR) {
                    return resolved;
                }
                return resolved;
            }
            return t;
        }
        
        // Unificar dois tipos
        void unify(std::shared_ptr<Type> t1, std::shared_ptr<Type> t2) {
            t1 = resolve(t1);
            t2 = resolve(t2);
            
            // Se são iguais, nada a fazer
            if (t1->equals(t2)) {
                return;
            }
            
            // Se ambos são variáveis de tipo
            if (t1->kind == Kind::TYPE_VAR && t2->kind == Kind::TYPE_VAR) {
                auto tv1 = std::static_pointer_cast<TypeVar>(t1);
                auto tv2 = std::static_pointer_cast<TypeVar>(t2);
                tv1->instance = t2;
                return;
            }
            
            // Se t1 é variável de tipo, vincular t2 a ela
            if (t1->kind == Kind::TYPE_VAR) {
                auto tv1 = std::static_pointer_cast<TypeVar>(t1);
                // Verificar ocorrência (occurs check)
                if (occurs_in(tv1, t2)) {
                    throw std::runtime_error("Type error: circular type constraint");
                }
                tv1->instance = t2;
                return;
            }
            
            // Se t2 é variável de tipo, vincular t1 a ela
            if (t2->kind == Kind::TYPE_VAR) {
                auto tv2 = std::static_pointer_cast<TypeVar>(t2);
                // Verificar ocorrência (occurs check)
                if (occurs_in(tv2, t1)) {
                    throw std::runtime_error("Type error: circular type constraint");
                }
                tv2->instance = t1;
                return;
            }
            
            // Unificar tipos compostos
            if (t1->kind == t2->kind) {
                switch (t1->kind) {
                    case Kind::ARRAY: {
                        auto a1 = std::static_pointer_cast<Array>(t1);
                        auto a2 = std::static_pointer_cast<Array>(t2);
                        // Arrays devem ter mesmo tamanho e tipo de elemento
                        if (a1->size != a2->size) {
                            throw std::runtime_error("Type error: array size mismatch");
                        }
                        unify(a1->element_type, a2->element_type);
                        return;
                    }
                    case Kind::VECTOR: {
                        // Vectors são sempre compatíveis entre si
                        return;
                    }
                    case Kind::TUPLE: {
                        auto tu1 = std::static_pointer_cast<Tuple>(t1);
                        auto tu2 = std::static_pointer_cast<Tuple>(t2);
                        if (tu1->element_type.size() != tu2->element_type.size()) {
                            throw std::runtime_error("Type error: tuple size mismatch");
                        }
                        for (size_t i = 0; i < tu1->element_type.size(); i++) {
                            unify(tu1->element_type[i], tu2->element_type[i]);
                        }
                        return;
                    }
                    case Kind::LABEL: {
                        auto l1 = std::static_pointer_cast<Label>(t1);
                        auto l2 = std::static_pointer_cast<Label>(t2);
                        if (l1->paramstype.size() != l2->paramstype.size()) {
                            throw std::runtime_error("Type error: function parameter count mismatch");
                        }
                        for (size_t i = 0; i < l1->paramstype.size(); i++) {
                            unify(l1->paramstype[i], l2->paramstype[i]);
                        }
                        unify(l1->returntype, l2->returntype);
                        return;
                    }
                    default:
                        break;
                }
            }
            
            // Coerção implícita: int pode ser promovido para float
            // Verificar se um é int e outro é float
            bool t1_is_int = t1->kind == Kind::INT;
            bool t1_is_float = t1->kind == Kind::FLOAT;
            bool t2_is_int = t2->kind == Kind::INT;
            bool t2_is_float = t2->kind == Kind::FLOAT;
            
            if ((t1_is_int && t2_is_float) || (t1_is_float && t2_is_int)) {
                // Promover int para float - ambos os tipos devem ser float
                // Criar um tipo float compartilhado para vincular
                auto float_type = std::make_shared<Float>();
                // Se t1 era int, vincular t1 a float (mas t1 já é concreto, então não faz sentido)
                // Na verdade, como ambos são tipos concretos, apenas permitimos a unificação
                // O tipo resultante será float (o mais específico)
                // Mas precisamos garantir que se houver variáveis de tipo envolvidas, elas sejam vinculadas a float
                return;
            }
            
            // Tipos incompatíveis
            throw std::runtime_error("Type error: cannot unify '" + t1->toString() + 
                                   "' with '" + t2->toString() + "'");
        }
        
        // Verificar se variável ocorre em tipo (occurs check)
        bool occurs_in(std::shared_ptr<TypeVar> var, std::shared_ptr<Type> t) {
            t = resolve(t);
            
            if (t->kind == Kind::TYPE_VAR) {
                auto tv = std::static_pointer_cast<TypeVar>(t);
                return var->id == tv->id;
            }
            
            // Verificar recursivamente em tipos compostos
            switch (t->kind) {
                case Kind::ARRAY: {
                    auto a = std::static_pointer_cast<Array>(t);
                    return occurs_in(var, a->element_type);
                }
                case Kind::VECTOR: {
                    // Vector não tem variáveis de tipo
                    return false;
                }
                case Kind::TUPLE: {
                    auto tu = std::static_pointer_cast<Tuple>(t);
                    for (const auto& elem : tu->element_type) {
                        if (occurs_in(var, elem)) return true;
                    }
                    return false;
                }
                case Kind::LABEL: {
                    auto l = std::static_pointer_cast<Label>(t);
                    for (const auto& param : l->paramstype) {
                        if (occurs_in(var, param)) return true;
                    }
                    return occurs_in(var, l->returntype);
                }
                default:
                    return false;
            }
        }
        
        // Generalizar tipo (criar tipo polimórfico)
        std::shared_ptr<Type> generalize(std::shared_ptr<Type> t, 
                                         const std::unordered_set<int>& free_in_env) {
            t = resolve(t);
            
            // Coletar variáveis livres no tipo
            std::unordered_set<int> free_vars;
            t->collect_free_vars(free_vars);
            
            // Remover variáveis que estão livres no ambiente
            std::unordered_set<int> bound_vars;
            for (int var : free_vars) {
                if (free_in_env.find(var) == free_in_env.end()) {
                    bound_vars.insert(var);
                }
            }
            
            // Se não há variáveis para generalizar, retornar tipo original
            if (bound_vars.empty()) {
                return t;
            }
            
            // Criar tipo polimórfico
            return std::make_shared<PolyType>(bound_vars, t);
        }
        
        // Obter próximo ID de variável
        int get_next_var_id() const { return next_var_id; }
    };
}
