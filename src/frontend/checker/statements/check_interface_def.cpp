#include "frontend/checker/statements/check_interface_def.hpp"
#include "frontend/ast/statements/interface_def_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"

namespace nv {
    std::shared_ptr<Type> check_interface_def(Checker* checker, Node* node) {
        auto* iface_def = static_cast<InterfaceDefNode*>(node);

        auto iface_type = std::make_shared<Interface>(iface_def->name);
        iface_type->parent_interfaces = iface_def->parent_interfaces;

        // Registra os métodos da interface com seus tipos
        for (const auto& method : iface_def->methods) {
            std::vector<std::shared_ptr<Type>> param_types;
            for (const auto& [pname, ptype] : method.params) {
                param_types.push_back(checker->gettyptr(ptype));
            }
            auto ret_type = method.return_type.empty() ? checker->gettyptr("void")
                                                       : checker->gettyptr(method.return_type);
            iface_type->add_method(method.name, std::make_shared<Def>(param_types, ret_type));
        }

        checker->types[iface_def->name] = iface_type;
        checker->scope->put_key(iface_def->name, iface_type, true);
        return iface_type;
    }
}
