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

bool rph::Label::equals(const std::shared_ptr<rph::Type> &other) const {
    if (this->kind != other->kind) return false;
    if (other->kind != Kind::LABEL) return false;

    auto other_label = std::dynamic_pointer_cast<Label>(other);
    if (!other_label) return false;

    if (this->paramstype.size() != other_label->paramstype.size()) return false;
    if (!this->returntype->equals(other_label->returntype)) return false;

    for (size_t i = 0; i < this->paramstype.size(); ++i) {
        if (!this->paramstype[i]->equals(other_label->paramstype[i])) {
            return false;
        }
    }

    return true;
}

bool rph::Array::equals(const std::shared_ptr<rph::Type>& other) const {
    if (other->kind != rph::Kind::ARRAY) return false;
    auto other_array = std::dynamic_pointer_cast<Array>(other);
    return element_type->equals(other_array->element_type);
}

bool rph::Tuple::equals(const std::shared_ptr<rph::Type>& other) const {
    if (other->kind != rph::Kind::TUPLE) return false;
    auto other_tuple = std::dynamic_pointer_cast<Tuple>(other);
    if (size != other_tuple->size) return false;
    for (size_t i = 0; i < size; i++) {
        if (!(element_type[i]->equals(other_tuple->element_type[i]))) return false;
    }
    return true;
    
}

// -------- PROTOTYPES --------

namespace rph { 
    auto str_type = std::make_shared<String>();
    auto int_type = std::make_shared<Int>();
    auto bool_type = std::make_shared<Boolean>();
    auto void_type = std::make_shared<Void>();
    auto float_type = std::make_shared<Float>();
    auto arr_str = std::make_shared<Array>(str_type);

    std::shared_ptr<Label> make_native_label(
        const std::vector<std::shared_ptr<Type>>& params,
        std::shared_ptr<Type> ret
    ) {
        return std::make_shared<Label>(params, ret);
    }

    void String::init_prototype() {
        prototype = std::make_shared<rph::Namespace>();

        auto arr_str = std::make_shared<Array>(str_type);

        // toUpperCase(): string
        prototype->put_key("toUpperCase", make_native_label({}, str_type));

        // toLowerCase(): string
        prototype->put_key("toLowerCase", make_native_label({}, str_type));
        
        // includes(search: string): boolean
        prototype->put_key("includes", make_native_label({str_type}, bool_type));
        
        // startsWith(prefix: string): boolean
        prototype->put_key("startsWith", make_native_label({str_type}, bool_type));

        // endsWith(suffix: string): boolean
        prototype->put_key("endsWith", make_native_label({str_type}, bool_type));
        
        // replace(old: string, new: string): string
        prototype->put_key("replace", make_native_label({str_type, str_type}, str_type));

        // split(sep: string): string[]
        prototype->put_key("split", make_native_label({str_type}, arr_str));
        
        // trim(): string
        prototype->put_key("trim", make_native_label({}, str_type));
    }
    
    void Array::init_prototype() {
        prototype = std::make_shared<rph::Namespace>();
        auto elem = element_type;
        // push(value: T): void
        prototype->put_key("push", make_native_label({elem}, void_type));
        
        // pop(): T
        prototype->put_key("pop", make_native_label({}, elem));

        // shift(): T
        prototype->put_key("shift", make_native_label({}, elem));

        // unshift(value: T): void
        prototype->put_key("unshift", make_native_label({elem}, void_type));

        // map(fn: (T): T): array[T]  (sem genéricos ainda)
        auto fn_param = std::vector<std::shared_ptr<Type>>{elem};
        auto fn_ret = elem;
        auto fn_type = make_native_label(fn_param, fn_ret);
        auto arr_t = std::make_shared<Array>(elem);
        prototype->put_key("map", make_native_label({fn_type}, arr_t));

        // filter(fn: (T): boolean): array[T]
        auto filter_fn = make_native_label({elem}, bool_type);
        prototype->put_key("filter", make_native_label({filter_fn}, std::make_shared<Array>(elem)));

        // forEach(fn: (T): void): void
        auto foreach_fn = make_native_label({elem}, void_type);
        prototype->put_key("forEach", make_native_label({foreach_fn}, void_type));

        // join(sep: string): string
        prototype->put_key("join", make_native_label({str_type}, str_type));
    }

    void Int::init_prototype() {
        prototype = std::make_shared<rph::Namespace>();

        prototype->put_key("toString", make_native_label({}, str_type));
    } 
}