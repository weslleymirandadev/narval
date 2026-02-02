#include "frontend/checker/type.hpp"
#include "frontend/checker/namespace.hpp"
#include "frontend/ast/ast.hpp"
#include <functional>
#include <algorithm>

std::shared_ptr<nv::Type> nv::Type::get_method(const std::string& name) const {
    if (!prototype) return nullptr;
    return prototype->get_key(name);
}

std::string nv::Def::toString() {
    std::string s = "def(";
    for (size_t i = 0; i < paramstype.size(); ++i) {
        s += paramstype[i]->toString();
        if (i < paramstype.size() - 1) s += ", "; 
    }
    s += "): " + returntype->toString();
    return s;
}

bool nv::Def::equals(const nv::Type &other) const {
    if (this->kind != other.kind) return false;
    if (other.kind != Kind::DEF) return false;

    auto other_def = dynamic_cast<const Def*>(&other);
    if (!other_def) return false;
    
    if (this->paramstype.size() != other_def->paramstype.size()) return false;
    if (!this->returntype->equals(*other_def->returntype)) return false;

    for (size_t i = 0; i < this->paramstype.size(); ++i) {
        if (!this->paramstype[i]->equals(*other_def->paramstype[i])) {
            return false;
        }
    }

    return true;
}

bool nv::Array::equals(const nv::Type& other) const {
    if (other.kind != nv::Kind::ARRAY) return false;
    auto other_array = dynamic_cast<const Array*>(&other);
    if (!other_array) return false;
    return size == other_array->size && element_type->equals(*other_array->element_type);
}

bool nv::Tuple::equals(const nv::Type& other) const {
    if (other.kind != nv::Kind::TUPLE) return false;
    auto other_tuple = dynamic_cast<const Tuple*>(&other);
    if (!other_tuple) return false;
    if (size != other_tuple->size) return false;
    for (size_t i = 0; i < size; i++) {
        if (!(element_type[i]->equals(*other_tuple->element_type[i]))) return false;
    }
    return true;
    
}


//--- PROTOTYPES

namespace nv { 
    static std::shared_ptr<Type> make_native_def(
        const std::vector<std::shared_ptr<Type>>& params,
        const std::shared_ptr<Type>& ret
    ) {
        return std::make_shared<Def>(params, ret);
    }

    void String::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }
    
    void Int::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    } 
    
    void Float::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }

    void Boolean::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }

    void Array::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }

    void Vector::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
        
        // Registrar métodos builtin para Vector: push e pop
        // Estes métodos são tratados especialmente no codegen (generate_call_expr.cpp)
        // Aqui apenas registramos tipos genéricos para permitir verificação de tipos
        
        // push: aceita 1 argumento de qualquer tipo, retorna void
        // Usamos um tipo polimórfico para aceitar qualquer tipo de argumento
        // O tipo real será inferido durante a verificação de tipos
        auto push_param_type = std::make_shared<nv::TypeVar>(-1); // ID temporário, será resolvido durante inferência
        std::vector<std::shared_ptr<nv::Type>> push_params = {push_param_type};
        auto push_type = std::make_shared<nv::Def>(push_params, std::make_shared<nv::Void>());
        prototype->put_key("push", push_type, true);
        
        // pop: não aceita argumentos, retorna o elemento removido (tipo genérico)
        // O tipo de retorno será inferido durante a verificação de tipos
        auto pop_return_type = std::make_shared<nv::TypeVar>(-2); // ID temporário, será resolvido durante inferência
        std::vector<std::shared_ptr<nv::Type>> pop_params = {};
        auto pop_type = std::make_shared<nv::Def>(pop_params, pop_return_type);
        prototype->put_key("pop", pop_type, true);
    }

    void Tuple::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }

    void Map::init_prototype() {
        prototype = std::make_shared<nv::Namespace>();
    }
}