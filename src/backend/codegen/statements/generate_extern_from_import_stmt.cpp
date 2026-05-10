#include "frontend/ast/statements/extern_from_import_stmt_node.hpp"
#include "backend/codegen/ffi.hpp"
#include "backend/codegen/ir_context.hpp"

void ExternFromImportStmtNode::codegen(nv::IRGenerationContext& ctx) {
    if (language == "Python") {
        if (is_wildcard && !wildcard_alias.empty()) {
            // `from extern "Python:mod" import * as alias` — namespace proxy
            nv::ffi::emit_python_namespace(library, wildcard_alias, ctx);
            return;
        }
        // `from extern "Python[:lib]" import name [as alias]`
        for (auto& item : imports) {
            std::string sym = item.alias.empty() ? item.name : item.alias;
            std::string module;
            if (library.empty() || library == language)
                module = item.name;
            else
                module = library + "." + item.name;
            nv::ffi::emit_python_namespace(module, sym, ctx);
        }
        return;
    }

    if (language == "C") {
        auto known = nv::ffi::c_lib_registry(library);

        if (is_wildcard) {
            for (auto& fn : known)
                nv::ffi::emit_c_func(fn, ctx);
        } else {
            for (auto& item : imports) {
                bool found = false;
                for (auto& fn : known) {
                    if (fn.name == item.name) {
                        nv::ffi::CLibFunc aliased = fn;
                        if (!item.alias.empty()) aliased.name = item.alias;
                        nv::ffi::emit_c_func(aliased, ctx);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    llvm::errs() << "narval: 'from extern \"C:" << library
                                 << "\" import " << item.name
                                 << "': not found in registry. Use extern \"C\" { def "
                                 << item.name << "(...): type } to declare manually.\n";
            }
        }
        return;
    }

    llvm::errs() << "narval: 'from extern' not yet supported for language '"
                 << language << "'. Use extern \"" << language << "\" { def ... } instead.\n";
}
