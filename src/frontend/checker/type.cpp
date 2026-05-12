#include "frontend/checker/type.hpp"
#include "frontend/checker/namespace.hpp"
#include "frontend/ast/ast.hpp"
#include <functional>
#include <algorithm>
#include <sstream>

std::shared_ptr<nv::Type> nv::Type::get_method(const std::string& name) const {
    if (!prototype) return nullptr;
    return prototype->get_key(name);
}

std::string nv::Function::toString() {
    std::string s = "Function(";
    for (size_t i = 0; i < paramstype.size(); ++i) {
        s += paramstype[i]->toString();
        if (i < paramstype.size() - 1) s += ", "; 
    }
    s += "): " + returntype->toString();
    return s;
}

bool nv::Function::equals(const nv::Type &other) const {
    if (this->kind != other.kind) return false;
    if (other.kind != Kind::FUNCTION) return false;

    auto other_function = static_cast<const Function*>(&other);
    
    if (this->paramstype.size() != other_function->paramstype.size()) return false;
    if (!this->returntype->equals(*other_function->returntype)) return false;

    for (size_t i = 0; i < this->paramstype.size(); ++i) {
        if (!this->paramstype[i]->equals(*other_function->paramstype[i])) {
            return false;
        }
    }

    return true;
}

bool nv::Array::equals(const nv::Type& other) const {
    if (other.kind != nv::Kind::ARRAY) return false;
    auto other_array = static_cast<const Array*>(&other);
    return size == other_array->size && element_type->equals(*other_array->element_type);
}

bool nv::Tuple::equals(const nv::Type& other) const {
    if (other.kind != nv::Kind::TUPLE) return false;
    auto other_tuple = static_cast<const Tuple*>(&other);
    if (size != other_tuple->size) return false;
    for (size_t i = 0; i < size; i++) {
        if (!(element_type[i]->equals(*other_tuple->element_type[i]))) return false;
    }
    return true;
    
}

//--- CLOSURE TYPE PARSING

namespace nv {
    // Verifica se uma string representa um tipo função
    bool is_function_type(const std::string& type_str) {
        return type_str.size() >= 2 && type_str.front() == '|' && type_str.find('|') != std::string::npos;
    }
    
    // Parse de tipo função: |param1: type1, param2: type2|: return_type
    std::shared_ptr<Type> parse_function_type(const std::string& type_str, std::function<std::shared_ptr<Type>(const std::string&)> type_resolver) {
        if (!is_function_type(type_str)) {
            throw std::runtime_error("Not a function type: " + type_str);
        }
        
        // Encontrar o separador principal |...|: return_type
        size_t main_pipe_pos = type_str.find_last_of('|');
        if (main_pipe_pos == std::string::npos || main_pipe_pos == 0) {
            throw std::runtime_error("Invalid function type format: " + type_str);
        }
        
        // Extrair parâmetros e tipo de retorno
        std::string params_str = type_str.substr(1, main_pipe_pos - 1);
        std::string return_part = type_str.substr(main_pipe_pos + 1);
        
        // Verificar se tem : return_type
        if (return_part.empty() || return_part[0] != ':') {
            throw std::runtime_error("Function type must have return type: " + type_str);
        }
        
        std::string return_type_str = return_part.substr(1); // Remover ':'
        
        // Parsear parâmetros
        std::vector<std::shared_ptr<Type>> param_types;
        if (!params_str.empty()) {
            std::stringstream ss(params_str);
            std::string param;
            
            while (std::getline(ss, param, ',')) {
                // Remover espaços em branco
                param.erase(0, param.find_first_not_of(" \t"));
                param.erase(param.find_last_not_of(" \t") + 1);
                
                if (!param.empty()) {
                    // Parsear "name: type"
                    size_t colon_pos = param.find(':');
                    if (colon_pos == std::string::npos) {
                        throw std::runtime_error("Parameter must have type: " + param);
                    }
                    
                    std::string param_type_str = param.substr(colon_pos + 1);
                    param_type_str.erase(0, param_type_str.find_first_not_of(" \t"));
                    param_type_str.erase(param_type_str.find_last_not_of(" \t") + 1);
                    
                    auto param_type = type_resolver(param_type_str);
                    param_types.push_back(param_type);
                }
            }
        }
        
        // Parsear tipo de retorno
        auto return_type = type_resolver(return_type_str);
        
        return std::make_shared<Function>(param_types, return_type);
    }
    
    // Criar string de tipo função a partir de parâmetros e retorno
    std::string create_function_type_string(const std::vector<std::pair<std::string, std::string>>& params, const std::string& return_type) {
        std::string result = "|";
        for (size_t i = 0; i < params.size(); ++i) {
            result += params[i].first + ":" + params[i].second;
            if (i < params.size() - 1) result += ", ";
        }
        result += "|:" + return_type;
        return result;
    }
}

//--- PROTOTYPES

namespace nv { 
    static std::shared_ptr<Type> make_native_def(
        const std::vector<std::shared_ptr<Type>>& params,
        const std::shared_ptr<Type>& ret
    ) {
        return std::make_shared<Function>(params, ret);
    }

    void nv::String::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();

        // Registrar métodos de string para o checker
        // toUpperCase(): string
        auto self_type = std::const_pointer_cast<Type>(shared_from_this());
        prototype->put_key("toUpperCase", make_native_def({}, self_type), true);

        // replace(old: string, new: string): string
        prototype->put_key("replace", make_native_def({ self_type, self_type }, self_type), true);

        // includes(sub: string): bool
        prototype->put_key("includes", make_native_def({ self_type }, std::make_shared<nv::Boolean>()), true);
    }
    
    void nv::Int::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    } 
    
    void nv::Float::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }

    void nv::Boolean::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }

    void nv::Array::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }

    void nv::Vector::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
        
        // Registrar métodos builtin para Vector: push e pop
        // Estes métodos são tratados especialmente no codegen (generate_call_expr.cpp)
        // Aqui apenas registramos tipos genéricos para permitir verificação de tipos
        
        // push: aceita 1 argumento de qualquer tipo, retorna void
        // Usamos um tipo polimórfico para aceitar qualquer tipo de argumento
        // O tipo real será inferido durante a verificação de tipos
        auto push_param_type = std::make_shared<nv::TypeVar>(-1); // ID temporário, será resolvido durante inferência
        std::vector<std::shared_ptr<nv::Type>> push_params = {push_param_type};
        auto push_type = std::make_shared<nv::Function>(push_params, std::make_shared<nv::Void>());
        prototype->put_key("push", push_type, true);
        
        // pop: não aceita argumentos, retorna o elemento removido (tipo genérico)
        // O tipo de retorno será inferido durante a verificação de tipos
        auto pop_return_type = std::make_shared<nv::TypeVar>(-2); // ID temporário, será resolvido durante inferência
        std::vector<std::shared_ptr<nv::Type>> pop_params = {};
        auto pop_type = std::make_shared<nv::Function>(pop_params, pop_return_type);
        prototype->put_key("pop", pop_type, true);
    }

    void nv::Tuple::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }

    void nv::Map::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }

    void nv::Void::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }
}