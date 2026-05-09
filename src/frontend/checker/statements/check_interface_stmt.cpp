#include "frontend/checker/statements/check_interface_stmt.hpp"
#include "frontend/ast/statements/interface_stmt_node.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"

namespace nv {
    std::shared_ptr<Type> check_interface_stmt(Checker* checker, Node* node) {
        auto* iface_stmt = static_cast<InterfaceStmtNode*>(node);

        auto iface_type = std::make_shared<Interface>(iface_stmt->name);
        iface_type->parent_interfaces = iface_stmt->parent_interfaces;

        // Registra os métodos da interface com seus tipos
        for (const auto& method : iface_stmt->methods) {
            std::vector<std::shared_ptr<Type>> param_types;
            for (const auto& [pname, ptype] : method.params) {
                param_types.push_back(checker->gettyptr(ptype));
            }
            auto ret_type = method.return_type.empty() ? checker->gettyptr("void")
                                                       : checker->gettyptr(method.return_type);
            iface_type->add_method(method.name, std::make_shared<Function>(param_types, ret_type));
        }

        checker->types[iface_stmt->name] = iface_type;
        checker->scope->put_key(iface_stmt->name, iface_type, true);
        return iface_type;
    }
}
