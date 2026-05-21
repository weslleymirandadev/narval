#include "frontend/checker/statements/check_extern_from_import_stmt.hpp"
#include "frontend/ast/statements/extern_from_import_stmt_node.hpp"
#include "frontend/checker/type.hpp"

// Registra os símbolos importados no escopo do checker.
// Python e demais linguagens: bypass total de verificação de tipos via python_namespaces.
// C: idem — o codegen usa o registry para gerar wrappers LLVM tipados;
//    o checker aceita qualquer chamada a esses símbolos sem verificar assinatura.
std::shared_ptr<nv::Type>& check_extern_from_import_stmt(nv::Checker* checker, Node* node) {
    auto* stmt = static_cast<ExternFromImportStmtNode*>(node);

    auto register_dynamic = [&](const std::string& sym) {
        checker->scope->put_key(sym, checker->gettyptr("None"), false);
        checker->python_namespaces.insert(sym);
    };

    if (stmt->is_wildcard && !stmt->wildcard_alias.empty()) {
        register_dynamic(stmt->wildcard_alias);
        return checker->gettyptr("None");
    }

    for (auto& item : stmt->imports) {
        std::string sym = item.alias.empty() ? item.name : item.alias;
        register_dynamic(sym);
    }

    return checker->gettyptr("None");
}
