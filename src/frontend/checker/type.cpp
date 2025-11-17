#include "frontend/checker/type.hpp"
#include "frontend/checker/namespace.hpp"
#include "frontend/ast/ast.hpp"
#include <functional>
#include <algorithm>

std::shared_ptr<rph::Type> rph::Type::get_method(const std::string& name) const {
    if (!prototype) return nullptr;
    return prototype->get_key(name);
}

std::string rph::Label::toString() {
    std::string s = "label(";
    for (size_t i = 0; i < paramstype.size(); ++i) {
        s += paramstype[i]->toString();
        if (i < paramstype.size() - 1) s += ", "; 
    }
    s += "): " + returntype->toString();
    return s;
}

bool rph::Label::equals(const rph::Type &other) const {
    if (this->kind != other.kind) return false;
    if (other.kind != Kind::LABEL) return false;

    auto other_label = dynamic_cast<const Label*>(&other);
    if (!other_label) return false;
    
    if (this->paramstype.size() != other_label->paramstype.size()) return false;
    if (!this->returntype->equals(*other_label->returntype)) return false;

    for (size_t i = 0; i < this->paramstype.size(); ++i) {
        if (!this->paramstype[i]->equals(*other_label->paramstype[i])) {
            return false;
        }
    }

    return true;
}

bool rph::Array::equals(const rph::Type& other) const {
    if (other.kind != rph::Kind::ARRAY) return false;
    auto other_array = dynamic_cast<const Array*>(&other);
    if (!other_array) return false;
    return element_type->equals(*other_array->element_type);
}

bool rph::Tuple::equals(const rph::Type& other) const {
    if (other.kind != rph::Kind::TUPLE) return false;
    auto other_tuple = dynamic_cast<const Tuple*>(&other);
    if (!other_tuple) return false;
    if (size != other_tuple->size) return false;
    for (size_t i = 0; i < size; i++) {
        if (!(element_type[i]->equals(*other_tuple->element_type[i]))) return false;
    }
    return true;
    
}


//--- PROTOTYPES

namespace rph { 
    static std::shared_ptr<Type> make_native_label(
        const std::vector<std::shared_ptr<Type>>& params,
        const std::shared_ptr<Type>& ret
    ) {
        return std::make_shared<Label>(params, ret);
    }

    void String::init_prototype() {
        prototype = std::make_shared<rph::Namespace>();
    }
    
    void Int::init_prototype() {
        prototype = std::make_shared<rph::Namespace>();
    } 
    
    void Float::init_prototype() {
        prototype = std::make_shared<rph::Namespace>();
    }

    void Boolean::init_prototype() {
        prototype = std::make_shared<rph::Namespace>();
    }

    void Array::init_prototype() {
        prototype = std::make_shared<rph::Namespace>();
    }

    void Tuple::init_prototype() {
        prototype = std::make_shared<rph::Namespace>();
    }
}