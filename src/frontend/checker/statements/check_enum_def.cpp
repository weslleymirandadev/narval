#include "frontend/checker/statements/check_enum_def.hpp"
#include "frontend/ast/statements/enum_def_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"

namespace nv {
    std::shared_ptr<Type> check_enum_def(Checker* checker, Node* node) {
        auto* enum_def = static_cast<EnumDefNode*>(node);

        auto enum_type = std::make_shared<Enum>(enum_def->name);

        int next_value = 0;
        for (const auto& variant : enum_def->variants) {
            int val = variant.has_explicit_value ? variant.explicit_value : next_value;
            next_value = val + 1;
            enum_type->add_variant(variant.name, val);
        }

        enum_type->init_prototype();

        checker->types[enum_def->name] = enum_type;
        checker->scope->put_key(enum_def->name, enum_type, true);

        return enum_type;
    }
}
