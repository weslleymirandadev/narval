// types.hpp
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include "namespace.hpp" // Adicionado include completo do Namespace

namespace nv {
    class Namespace;
    enum Kind {
        STRING,
        CHAR,
        INT,
        FLOAT,
        BOOL,
        NONE,
        FUNCTION,
        ARRAY,
        TUPLE,
        VECTOR,
        MAP,
        CLASS,
        INTERFACE,
        ENUM,
        OPTION,
        RESULT,
        FUTURE,
        TYPE_VAR,
        POLY_TYPE,
        ERROR,
        LOW_LEVEL
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

        // Inicializar prototype (padrão: não faz nada)
        virtual void init_prototype() {}

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
        String() : Type(Kind::STRING) {}
        void init_prototype() override;

        std::string toString() override { return "str"; }
    };

    struct Char : public Type {
        Char() : Type(Kind::CHAR) {}
        void init_prototype() override;

        std::string toString() override { return "char"; }
    };

    struct Int : public Type {
        Int() : Type(Kind::INT) {}
        void init_prototype() override;

        std::string toString() override { return "int"; };
    };

    struct Float : public Type {
        Float() : Type(Kind::FLOAT) {}
        void init_prototype() override;

        std::string toString() override { return "float"; };
    };

    struct Boolean : public Type {
        Boolean() : Type(Kind::BOOL) {}
        void init_prototype() override;

        std::string toString() override { return "boolean"; };
    };

    struct None : public Type {
        None() : Type(Kind::NONE) {}
        void init_prototype() override;
        std::string toString() override { return "None"; }
    };

    // Tipos low-level com largura explícita para funções @[abi(...)] e asm
    struct LowLevelType : public Type {
        std::string type_name; // "i8", "i64", "u32", "f32", "ptr", etc.
        bool is_signed;
        int bit_width; // 8,16,32,64,128 — 0 para ptr

        LowLevelType(const std::string& name, bool sign, int bits)
            : Type(Kind::LOW_LEVEL), type_name(name), is_signed(sign), bit_width(bits) {}

        std::string toString() override { return type_name; }
        bool equals(const Type& other) const override {
            if (other.kind != Kind::LOW_LEVEL) return false;
            return type_name == static_cast<const LowLevelType&>(other).type_name;
        }
    };

    struct Class : public Type {
        std::string name;
        std::shared_ptr<Class> parent_class;
        std::shared_ptr<Type> parent_builtin_type;  // Para herança de tipos builtin
        bool is_builtin_derived = false;
        bool is_abstract = false;
        std::unordered_map<std::string, std::shared_ptr<Type>> fields;
        std::unordered_map<std::string, std::shared_ptr<Type>> methods;
        std::unordered_map<std::string, std::string> method_access; // method_name -> "public"/"private"/"protected"
        
        Class(const std::string& class_name) : Type(Kind::CLASS), name(class_name) {}
        
        std::string toString() override { return name; }
        
        bool equals(const Type &other) const override {
            if (other.kind != Kind::CLASS) return false;
            const Class* other_class = static_cast<const Class*>(&other);
            return name == other_class->name;
        }
        
        bool equals(std::shared_ptr<Type> &other) const override {
            if (other->kind != Kind::CLASS) return false;
            auto other_class = std::static_pointer_cast<Class>(other);
            return name == other_class->name;
        }
        
        void add_field(const std::string& field_name, std::shared_ptr<Type> field_type, bool is_mutable = false) {
            fields[field_name] = field_type;
        }
        
        void add_method(const std::string& method_name, std::shared_ptr<Type> method_type, const std::string& access = "public") {
            methods[method_name] = method_type;
            method_access[method_name] = access;
        }

        bool is_method_accessible(const std::string& method_name, const std::string& from_class) const {
            auto it = method_access.find(method_name);
            if (it == method_access.end()) return true; // sem info = acessível (builtins)
            if (it->second == "public") return true;
            // private e protected: apenas da própria classe
            return from_class == name;
        }
        
        std::shared_ptr<Type> get_field(const std::string& field_name) const {
            auto it = fields.find(field_name);
            if (it != fields.end()) {
                return it->second;
            }
            
            // Verificar na classe pai
            if (parent_class) {
                return parent_class->get_field(field_name);
            }
            
            return nullptr;
        }
        
        std::shared_ptr<Type> get_method(const std::string& method_name) const {
            auto it = methods.find(method_name);
            if (it != methods.end()) {
                return it->second;
            }
            
            // Verificar na classe pai
            if (parent_class) {
                return parent_class->get_method(method_name);
            }
            
            return nullptr;
        }
        
        void init_prototype() override {
            prototype = std::shared_ptr<Namespace>(new Namespace(nullptr));
            
            // Adicionar campos ao prototype
            for (const auto& field : fields) {
                prototype->put_key(field.first, field.second, true);
            }
            
            // Adicionar métodos ao prototype
            for (const auto& method : methods) {
                prototype->put_key(method.first, method.second, true);
            }
        }
    };

    struct Interface : public Type {
        std::string name;
        std::vector<std::string> parent_interfaces;
        std::unordered_map<std::string, std::shared_ptr<Type>> methods;

        Interface(const std::string& iname) : Type(Kind::INTERFACE), name(iname) {}

        std::string toString() override { return name; }

        bool equals(const Type& other) const override {
            if (other.kind != Kind::INTERFACE) return false;
            return name == static_cast<const Interface*>(&other)->name;
        }
        bool equals(std::shared_ptr<Type>& other) const override { return equals(*other); }

        void add_method(const std::string& mname, std::shared_ptr<Type> mtype) {
            methods[mname] = std::move(mtype);
        }

        // Retorna todos os métodos incluindo os herdados de parent interfaces
        std::unordered_map<std::string, std::shared_ptr<Type>> all_methods(
            const std::unordered_map<std::string, std::shared_ptr<Type>>& types) const
        {
            auto result = methods;
            for (const auto& pname : parent_interfaces) {
                auto it = types.find(pname);
                if (it != types.end() && it->second->kind == Kind::INTERFACE) {
                    auto* parent = static_cast<Interface*>(it->second.get());
                    for (auto& [mn, mt] : parent->all_methods(types)) {
                        if (result.find(mn) == result.end())
                            result[mn] = mt;
                    }
                }
            }
            return result;
        }
    };

    struct Enum : public Type {
        std::string name;
        std::vector<std::pair<std::string, int>> variants;

        Enum(const std::string& enum_name) : Type(Kind::ENUM), name(enum_name) {}

        std::string toString() override { return name; }

        bool equals(const Type& other) const override {
            if (other.kind != Kind::ENUM) return false;
            return name == static_cast<const Enum*>(&other)->name;
        }

        bool equals(std::shared_ptr<Type>& other) const override {
            if (other->kind != Kind::ENUM) return false;
            return name == std::static_pointer_cast<Enum>(other)->name;
        }

        void add_variant(const std::string& variant_name, int value) {
            variants.push_back({variant_name, value});
        }

        void init_prototype() override {
            prototype = std::shared_ptr<Namespace>(new Namespace(nullptr));
            auto int_type = std::make_shared<Int>();
            for (const auto& [var_name, val] : variants) {
                prototype->put_key(var_name, int_type, true);
            }
        }
    };

    struct Option : public Type {
        std::shared_ptr<Type> element_type;

        Option(std::shared_ptr<Type> elem) : Type(Kind::OPTION), element_type(std::move(elem)) {}

        std::string toString() override {
            return "Option<" + element_type->toString() + ">";
        }
        bool equals(const Type& other) const override {
            if (other.kind != Kind::OPTION) return false;
            return element_type->equals(*static_cast<const Option*>(&other)->element_type);
        }
        bool equals(std::shared_ptr<Type>& other) const override { return equals(*other); }
        void collect_free_vars(std::unordered_set<int>& fv) const override { element_type->collect_free_vars(fv); }
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& s) const override {
            return std::make_shared<Option>(element_type->substitute(s));
        }
    };

    struct Result : public Type {
        std::shared_ptr<Type> ok_type;
        std::shared_ptr<Type> err_type;

        Result(std::shared_ptr<Type> ok, std::shared_ptr<Type> err)
            : Type(Kind::RESULT), ok_type(std::move(ok)), err_type(std::move(err)) {}

        std::string toString() override {
            return "Result<" + ok_type->toString() + ", " + err_type->toString() + ">";
        }
        bool equals(const Type& other) const override {
            if (other.kind != Kind::RESULT) return false;
            const Result* r = static_cast<const Result*>(&other);
            return ok_type->equals(*r->ok_type) && err_type->equals(*r->err_type);
        }
        bool equals(std::shared_ptr<Type>& other) const override { return equals(*other); }
        void collect_free_vars(std::unordered_set<int>& fv) const override {
            ok_type->collect_free_vars(fv); err_type->collect_free_vars(fv);
        }
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& s) const override {
            return std::make_shared<Result>(ok_type->substitute(s), err_type->substitute(s));
        }
    };

    struct Future : public Type {
        std::shared_ptr<Type> element_type;

        Future(std::shared_ptr<Type> elem) : Type(Kind::FUTURE), element_type(std::move(elem)) {}

        std::string toString() override {
            return "Future<" + element_type->toString() + ">";
        }
        bool equals(const Type& other) const override {
            if (other.kind != Kind::FUTURE) return false;
            return element_type->equals(*static_cast<const Future*>(&other)->element_type);
        }
        bool equals(std::shared_ptr<Type>& other) const override { return equals(*other); }
        void collect_free_vars(std::unordered_set<int>& fv) const override { element_type->collect_free_vars(fv); }
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& s) const override {
            return std::make_shared<Future>(element_type->substitute(s));
        }
    };

    struct Function : public Type {
        std::vector<std::shared_ptr<Type>> paramstype;
        std::shared_ptr<Type> returntype;
        Function(const std::vector<std::shared_ptr<Type>>& params,
                  const std::shared_ptr<Type>& returns)
            : Type(Kind::FUNCTION), paramstype(params), returntype(returns) {}
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
            return std::make_shared<Function>(new_params, new_ret);
        }
    };

    struct Array : public Type {
        std::shared_ptr<Type> element_type;
        size_t size; // tamanho fixo (sempre > 0 para arrays)

        Array(const std::shared_ptr<Type>& elem, size_t sz)
            : Type(Kind::ARRAY), element_type(elem), size(sz) {}
        void init_prototype() override;

        std::string toString() override {
            return element_type->toString() + "[" + std::to_string(size) + "]";
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
        Vector() : Type(Kind::VECTOR) {}
        void init_prototype() override;

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
            : Type(Kind::TUPLE), element_type(typ), size(typ.size()) {};
        void init_prototype() override;
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

    struct Map : public Type {
        std::shared_ptr<Type> key_type;
        std::shared_ptr<Type> value_type;

        Map(const std::shared_ptr<Type>& key, const std::shared_ptr<Type>& val)
            : Type(Kind::MAP), key_type(key), value_type(val) {}
        void init_prototype() override;
        
        std::string toString() override {
            return "map<" + key_type->toString() + ", " + value_type->toString() + ">";
        }
        
        bool equals(const Type& other) const override {
            if (other.kind != Kind::MAP) return false;
            const Map* m = static_cast<const Map*>(&other);
            return key_type->equals(*m->key_type) && value_type->equals(*m->value_type);
        }
        
        bool equals(std::shared_ptr<Type>& other) const override {
            return equals(*other);
        }
        
        std::shared_ptr<Type> get_length() const override {
            return std::make_shared<Int>();
        }
        
        void collect_free_vars(std::unordered_set<int>& free_vars) const override {
            key_type->collect_free_vars(free_vars);
            value_type->collect_free_vars(free_vars);
        }
        
        std::shared_ptr<Type> substitute(const std::unordered_map<int, std::shared_ptr<Type>>& subst) const override {
            return std::make_shared<Map>(key_type->substitute(subst), value_type->substitute(subst));
        }
    };

    //--- CLOSURE TYPE PARSING FUNCTIONS
    
    // Verifica se uma string representa um tipo função
    bool is_function_type(const std::string& type_str);
    
    // Parse de tipo função: |param1: type1, param2: type2|: return_type
    std::shared_ptr<Type> parse_function_type(const std::string& type_str, std::function<std::shared_ptr<Type>(const std::string&)> type_resolver);
    
    // Criar string de tipo função a partir de parâmetros e retorno
    std::string create_function_type_string(const std::vector<std::pair<std::string, std::string>>& params, const std::string& return_type);
};
