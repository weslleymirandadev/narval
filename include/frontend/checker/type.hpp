// types.hpp
#pragma once
#include <string>
#include <memory>
#include <vector>

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
        ERROR
    };
    struct Type {
        Kind kind;
        // apenas para função: param types + return
        std::vector<std::shared_ptr<Type>> params;
        std::shared_ptr<Type> ret;
        std::shared_ptr<Namespace> prototype;

        Type(Kind k) : kind(k) {}
        virtual bool equals(const std::shared_ptr<Type> &other) const { return this->kind == other->kind; };
        virtual std::string toString() = 0;

        std::shared_ptr<Type> get_method(const std::string& name) const;

        virtual std::shared_ptr<Type> get_length() const { return nullptr; }
    };

    struct String : public Type {
        String() : Type(Kind::STRING) { init_prototype(); }
        void init_prototype();

        std::string toString() override { return "string"; }
    };

    struct Int : public Type {
        Int() : Type(Kind::INT) { init_prototype(); }
        void init_prototype();

        std::string toString() override { return "integer"; };
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
        Label(std::vector<std::shared_ptr<Type>> params, std::shared_ptr<Type> returns)
            : Type(Kind::LABEL), paramstype(params), returntype(returns) {}
        std::string toString() override;
        bool equals(const std::shared_ptr<Type> &other) const override;
    };

    struct Array : public Type {
        std::shared_ptr<Type> element_type;
        size_t size; // 0 = dynamic

        Array(std::shared_ptr<Type> elem, size_t sz = 0)
            : Type(Kind::ARRAY), element_type(elem), size(sz) { init_prototype(); }
        void init_prototype();

        std::string toString() override {
            return "array[" + element_type->toString() + "]";
        }
        bool equals(const std::shared_ptr<Type>& other) const override;
        std::shared_ptr<Type> get_length() const override {
            return std::make_shared<Int>();
        }
    };

    struct Tuple : public Type {
        std::vector<std::shared_ptr<Type>> element_type;
        size_t size; // 0 = dynamic

        Tuple(std::vector<std::shared_ptr<Type>> typ) : Type(Kind::TUPLE), element_type(typ) {
            init_prototype();
        };
        void init_prototype();
        bool equals(const std::shared_ptr<Type>& other) const override;
        std::string toString() override;
    };
};