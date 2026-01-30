// types.hpp
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>

namespace rph {
    class Namespace;
    enum Kind {
        STRING,
        INT,
        FLOAT,
        BOOL,
        VOID,
        LABEL,
        ARRAY,
        TUPLE,
        VECTOR,
        TYPE_VAR,
        POLY_TYPE,
        ERROR
    };
    struct Type : public std::enable_shared_from_this<Type> {
        Kind kind;
        // apenas para função: param types + return
        std::vector<std::shared_ptr<Type>> params;
        std::shared_ptr<Type> ret;
        std::shared_ptr<Namespace> prototype;

        Type(Kind k) : kind(k) {}
        virtual ~Type() = default;
        virtual bool equals(const Type &other) const { return this->kind == other.kind; };
        virtual bool equals(std::shared_ptr<Type> &other) const { return this->kind == other->kind; };
        virtual std::string toString() = 0;

        std::shared_ptr<Type> get_method(const std::string& name) const;

        virtual std::shared_ptr<Type> get_length() const { return nullptr; }
        
        // Para inferência de tipos
        virtual std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& subst) const {
            // Para tipos básicos sem variáveis de tipo, retornar o próprio tipo
            // Isso requer que o tipo esteja em um shared_ptr, o que é sempre o caso no nosso sistema
            return std::const_pointer_cast<Type>(const_cast<Type*>(this)->shared_from_this());
        }
        
        // Coletar variáveis de tipo livres
        virtual void collect_free_vars(std::unordered_set<int>& free_vars) const {}
        
        // Verificar se é uma variável de tipo
        virtual bool is_type_var() const { return false; }
    };
    
    // Variável de tipo para inferência Hindley-Milner
    struct TypeVar : public Type {
        int id; // identificador único da variável
        std::shared_ptr<Type> instance; // instância resolvida (para unificação)
        
        TypeVar(int var_id) : Type(Kind::TYPE_VAR), id(var_id), instance(nullptr) {}
        
        std::string toString() override {
            if (instance) {
                return instance->toString();
            }
            return "'t" + std::to_string(id);
        }
        
        bool equals(const Type &other) const override {
            if (other.kind == Kind::TYPE_VAR) {
                const TypeVar* tv = static_cast<const TypeVar*>(&other);
                if (instance && tv->instance) {
                    return instance->equals(*tv->instance);
                }
                return id == tv->id;
            }
            if (instance) {
                return instance->equals(other);
            }
            return false;
        }
        
        bool equals(std::shared_ptr<Type> &other) const override {
            if (other->kind == Kind::TYPE_VAR) {
                auto tv = std::static_pointer_cast<TypeVar>(other);
                if (instance && tv->instance) {
                    return instance->equals(tv->instance);
                }
                return id == tv->id;
            }
            if (instance) {
                return instance->equals(other);
            }
            return false;
        }
        
        bool is_type_var() const override { return true; }
        
        void collect_free_vars(std::unordered_set<int>& free_vars) const override {
            if (!instance) {
                free_vars.insert(id);
            } else {
                instance->collect_free_vars(free_vars);
            }
        }
        
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& subst) const override {
            if (instance) {
                return instance->substitute(subst);
            }
            auto it = subst.find(id);
            if (it != subst.end()) {
                return it->second;
            }
            // Retornar nova variável de tipo com mesmo ID (não substituída)
            return std::make_shared<TypeVar>(id);
        }
        
        // Resolver instância (path compression)
        std::shared_ptr<Type> resolve() {
            if (instance && instance->kind == Kind::TYPE_VAR) {
                auto tv = std::static_pointer_cast<TypeVar>(instance);
                instance = tv->resolve();
            }
            return instance ? instance : std::static_pointer_cast<Type>(shared_from_this());
        }
    };
    
    // Tipo polimórfico (Scheme) para generalização
    struct PolyType : public Type {
        std::unordered_set<int> bound_vars; // variáveis de tipo quantificadas
        std::shared_ptr<Type> body; // tipo base
        
        PolyType(const std::unordered_set<int>& vars, std::shared_ptr<Type> b)
            : Type(Kind::POLY_TYPE), bound_vars(vars), body(b) {}
        
        std::string toString() override {
            std::string result = "∀";
            bool first = true;
            for (int var : bound_vars) {
                if (!first) result += ",";
                result += "'t" + std::to_string(var);
                first = false;
            }
            result += ". " + body->toString();
            return result;
        }
        
        bool equals(const Type &other) const override {
            if (other.kind != Kind::POLY_TYPE) return false;
            const PolyType* pt = static_cast<const PolyType*>(&other);
            if (bound_vars.size() != pt->bound_vars.size()) return false;
            // Para tipos polimórficos, precisamos de alpha-equivalência
            // Simplificação: comparar estruturalmente
            return body->equals(*pt->body);
        }
        
        bool equals(std::shared_ptr<Type> &other) const override {
            return equals(*other);
        }
        
        void collect_free_vars(std::unordered_set<int>& free_vars) const override {
            body->collect_free_vars(free_vars);
            for (int var : bound_vars) {
                free_vars.erase(var);
            }
        }
        
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& subst) const override {
            auto new_subst = subst;
            for (int var : bound_vars) {
                new_subst.erase(var);
            }
            return body->substitute(new_subst);
        }
        
        // Instanciar tipo polimórfico (criar novas variáveis de tipo)
        std::shared_ptr<Type> instantiate(int& next_var_id) const {
            std::unordered_map<int, std::shared_ptr<Type>> subst;
            for (int var : bound_vars) {
                subst[var] = std::make_shared<TypeVar>(next_var_id++);
            }
            return body->substitute(subst);
        }
    };

    struct String : public Type {
        String() : Type(Kind::STRING) { init_prototype(); }
        void init_prototype();

        std::string toString() override { return "string"; }
    };

    struct Int : public Type {
        Int() : Type(Kind::INT) { init_prototype(); }
        void init_prototype();

        std::string toString() override { return "int"; };
    };

    struct Float : public Type {
        Float() : Type(Kind::FLOAT) { init_prototype(); }
        void init_prototype();

        std::string toString() override { return "float"; };
    };

    struct Boolean : public Type {
        Boolean() : Type(Kind::BOOL) { init_prototype() ;}
        void init_prototype();

        std::string toString() override { return "boolean"; };
    };

    struct Void : public Type {
        Void() : Type(Kind::VOID) {}
        std::string toString() override { return "()"; }
    };

    struct Label : public Type {
        std::vector<std::shared_ptr<Type>> paramstype;
        std::shared_ptr<Type> returntype;
        Label(const std::vector<std::shared_ptr<Type>>& params,
              const std::shared_ptr<Type>& returns)
            : Type(Kind::LABEL), paramstype(params), returntype(returns) {}
        std::string toString() override;
        bool equals(const Type &other) const override;
        
        void collect_free_vars(std::unordered_set<int>& free_vars) const override {
            for (const auto& param : paramstype) {
                param->collect_free_vars(free_vars);
            }
            returntype->collect_free_vars(free_vars);
        }
        
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& subst) const override {
            std::vector<std::shared_ptr<Type>> new_params;
            for (const auto& param : paramstype) {
                new_params.push_back(param->substitute(subst));
            }
            auto new_ret = returntype->substitute(subst);
            return std::make_shared<Label>(new_params, new_ret);
        }
    };

    struct Array : public Type {
        std::shared_ptr<Type> element_type;
        size_t size; // tamanho fixo (sempre > 0 para arrays)

        Array(const std::shared_ptr<Type>& elem, size_t sz)
            : Type(Kind::ARRAY), element_type(elem), size(sz) { init_prototype(); }
        void init_prototype();

        std::string toString() override {
            return "array<" + element_type->toString() + ">";
        }
        bool equals(const Type& other) const override;
        std::shared_ptr<Type> get_length() const override {
            return std::make_shared<Int>();
        }
        
        void collect_free_vars(std::unordered_set<int>& free_vars) const override {
            element_type->collect_free_vars(free_vars);
        }
        
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& subst) const override {
            return std::make_shared<Array>(element_type->substitute(subst), size);
        }
    };

    struct Vector : public Type {
        // Vector heterogêneo ou de tamanho variável
        // Não tem tipo de elemento específico (pode conter qualquer tipo)
        Vector() : Type(Kind::VECTOR) { init_prototype(); }
        void init_prototype();

        std::string toString() override {
            return "vector";
        }
        bool equals(const Type& other) const override {
            return other.kind == Kind::VECTOR;
        }
        std::shared_ptr<Type> get_length() const override {
            return std::make_shared<Int>();
        }
        
        void collect_free_vars(std::unordered_set<int>& free_vars) const override {
            // Vector não tem variáveis de tipo livres
        }
        
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& subst) const override {
            return std::make_shared<Vector>();
        }
    };

    struct Tuple : public Type {
        std::vector<std::shared_ptr<Type>> element_type;
        size_t size; // 0 = dynamic

        Tuple(const std::vector<std::shared_ptr<Type>>& typ)
            : Type(Kind::TUPLE), element_type(typ), size(typ.size()) {
            init_prototype();
        };
        void init_prototype();
        bool equals(const Type& other) const override;
        
        std::string toString() override {
            std::string result = "(";
            for (size_t i = 0; i < element_type.size(); i++) {
                result += element_type[i]->toString();
                if (i < element_type.size() - 1) {
                    result += ", ";
                }
            }
            result += ")";
            return result;
        }
        
        void collect_free_vars(std::unordered_set<int>& free_vars) const override {
            for (const auto& elem : element_type) {
                elem->collect_free_vars(free_vars);
            }
        }
        
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& subst) const override {
            std::vector<std::shared_ptr<Type>> new_elements;
            for (const auto& elem : element_type) {
                new_elements.push_back(elem->substitute(subst));
            }
            return std::make_shared<Tuple>(new_elements);
        }
    };
};