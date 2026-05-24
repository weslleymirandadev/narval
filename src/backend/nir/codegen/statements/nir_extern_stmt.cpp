#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/extern_stmt_node.hpp"
#include "frontend/ast/statements/extern_from_import_stmt_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include <string>
#include <vector>
#include <utility>

// Forward-declare what we need from ffi.cpp without pulling in the LLVM IR context.
namespace nv::ffi {
struct CLibFunc {
    std::string name;
    std::string return_type;
    std::vector<std::pair<std::string, std::string>> params; // (name, narval_type)
};
std::vector<CLibFunc> c_lib_registry(const std::string& libname);
} // namespace nv::ffi

// Declare each extern C function in the MLIR module so callers can reference it.
void ExternStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& mlir_ctx = ctx.get_mlir_context();
    auto  vt       = ctx.get_narval_value_type();
    auto& b        = ctx.get_builder();
    auto  ul       = b.getUnknownLoc();

    for (const auto& decl : declarations) {
        // Skip if already declared.
        if (ctx.get_module().lookupSymbol(decl.name)) continue;

        // Build a simple all-Value function type (narval convention).
        size_t nparams = 0;
        for (const auto& p : decl.parameters)
            nparams += p.parameter.size();

        std::vector<mlir::Type> ptypes(nparams, vt);
        bool is_void = (decl.return_type == "None" || decl.return_type == "void"
                        || decl.return_type.empty());
        mlir::TypeRange rtypes = is_void ? mlir::TypeRange{} : mlir::TypeRange{vt};

        auto fn_type = mlir::FunctionType::get(&mlir_ctx, ptypes, rtypes);
        auto fn = mlir::func::FuncOp::create(b, ul, decl.name, fn_type);
        fn.setPublic();
        ctx.get_module().push_back(fn);
    }
}

// Register one C FFI function: declare the nv_ffi_<fn.name> bridge in the module
// and register user_name -> bridge_name in the call remap table.
//
// This avoids emitting a wrapper function named @<fn.name> which would shadow
// the C library symbol of the same name (causing infinite recursion in the
// bridge's own math.h call).
static void register_c_ffi(nv::NIRGenerationContext& ctx,
                            const nv::ffi::CLibFunc& fn,
                            const std::string& user_name) {
    std::string bridge = "nv_ffi_" + fn.name;
    // Map user-visible name to the bridge so nir_call_expr redirects correctly.
    // The bridge declaration is emitted lazily when the call site is processed
    // by nir_call_expr (falls through to nir_call_runtime → CallRuntimeOp).
    ctx.register_ffi_remap(user_name, bridge);
}

// `from extern "C:lib" import ...` — look up the FFI registry and register remaps.
void ExternFromImportStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    if (language != "C") return; // Python and other languages not yet in NIR path.

    auto known = nv::ffi::c_lib_registry(library);
    if (known.empty()) return;

    if (is_wildcard) {
        for (const auto& fn : known)
            register_c_ffi(ctx, fn, fn.name);
    } else {
        for (const auto& item : imports) {
            for (const auto& fn : known) {
                if (fn.name == item.name) {
                    std::string user_name = item.alias.empty() ? item.name : item.alias;
                    register_c_ffi(ctx, fn, user_name);
                    break;
                }
            }
        }
    }
}
