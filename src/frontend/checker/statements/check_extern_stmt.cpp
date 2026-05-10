#include "frontend/checker/statements/check_extern_stmt.hpp"
#include "frontend/ast/statements/extern_stmt_node.hpp"
#include "frontend/checker/type.hpp"

std::shared_ptr<nv::Type>& check_extern_stmt(nv::Checker* checker, Node* node) {
    auto* ext = static_cast<ExternStmtNode*>(node);

    for (auto& decl : ext->declarations) {
        // Construir o tipo de função com base nos parâmetros e retorno
        std::vector<std::shared_ptr<nv::Type>> param_types;
        for (auto& param : decl.parameters) {
            for (auto& [name, type_str] : param.parameter) {
                std::shared_ptr<nv::Type> pt;
                try { pt = checker->gettyptr(type_str); } catch (...) { pt = checker->gettyptr("int"); }
                param_types.push_back(pt);
            }
        }

        std::shared_ptr<nv::Type> ret_type;
        try { ret_type = checker->gettyptr(decl.return_type); } catch (...) { ret_type = checker->gettyptr("void"); }

        auto func_type = std::make_shared<nv::Function>(param_types, ret_type);
        checker->scope->put_key(decl.name, func_type, true);

        // Registrar nomes dos parâmetros para suporte a keyword arguments,
        // igual ao que check_function_stmt faz para funções Narval.
        std::vector<std::string> pnames;
        std::vector<bool> has_default;
        for (auto& param : decl.parameters) {
            for (auto& [name, type_str] : param.parameter) {
                pnames.push_back(name);
                has_default.push_back(false); // extern não tem defaults
            }
        }
        if (!pnames.empty()) {
            checker->function_param_names[decl.name]    = pnames;
            checker->function_param_defaults[decl.name] = has_default;
        }
    }

    return checker->gettyptr("void");
}
