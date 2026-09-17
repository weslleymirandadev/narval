#include "frontend/checker/statements/check_extern_from_import_stmt.hpp"
#include "frontend/ast/statements/extern_from_import_stmt_node.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/ffi/c_lib_registry.hpp"

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

    // `from extern "C:math" import *` — register every function of the library. The
    // list is the same one the codegen uses to build the `nv_ffi_<name>` bridges, so
    // what the checker accepts is exactly what the backend can lower.
    if (stmt->is_wildcard) {
        for (const auto& fn : nv::ffi::c_lib_registry(stmt->library))
            register_dynamic(fn.name);
        return checker->gettyptr("None");
    }

    for (auto& item : stmt->imports) {
        std::string sym = item.alias.empty() ? item.name : item.alias;
        register_dynamic(sym);
    }

    return checker->gettyptr("None");
}
