#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/class_stmt_node.hpp"
#include "frontend/ast/statements/function_stmt_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

// Emit one class method.
//
// The method is emitted as a `narval.func` (mirroring FunctionStmtNode) rather
// than a raw func.func: narval.func goes through LowerNarvalFunctionsPass, which
// converts the signature to the runtime pointer ABI exactly like a plain `def`.
// Emitting func.func directly baked !narval.value into the ABI and left
// unresolved materializations at the call site.
//
//   constructor ("new") -> __ctor_<Class>(self, p0, ...) -> void
//   other methods       -> __method_<Class>_<name>(self, p0, ...) -> <ret>
static void nir_emit_class_method(nv::NIRGenerationContext& ctx,
                                   const std::string& class_name,
                                   const std::string& method_name,
                                   FunctionStmtNode* fn_node)
{
    auto& b        = ctx.get_builder();
    auto& mlir_ctx = ctx.get_mlir_context();
    auto  vt       = ctx.get_narval_value_type();

    mlir::OpBuilder::InsertionGuard guard(b);

    bool is_ctor = (method_name == "new");
    std::string fn_name = is_ctor
        ? "__ctor_" + class_name
        : "__method_" + class_name + "_" + method_name;

    if (ctx.get_module().lookupSymbol(fn_name)) return; // already emitted

    auto loc = ctx.loc(fn_node->position.get());

    // self + declared params, all !narval.value at this stage.
    size_t nparams = 1;
    for (const auto& p : fn_node->parameters)
        nparams += p.parameter.size();

    std::vector<mlir::Type> param_types(nparams, vt);
    bool is_void_ret = is_ctor
        || (fn_node->return_type == "None"
            || fn_node->return_type == "void"
            || fn_node->return_type.empty());

    // Never build a TypeRange from a braced initializer list: it stores an
    // ArrayRef into the temporary backing array, so the FunctionType would be
    // uniqued with a dangling (garbage) result type.
    std::vector<mlir::Type> ret_types;
    if (!is_void_ret) ret_types.push_back(vt);
    auto fn_type = mlir::FunctionType::get(&mlir_ctx, param_types, ret_types);

    // func-like ops must live in the module body (the symbol table op). Creating
    // it at the caller's insertion point nests it inside whichever function is
    // being emitted and corrupts the module symbol table (segfault on the next
    // lookupSymbol).
    b.setInsertionPointToEnd(ctx.get_module().getBody());
    auto fn = mlir::narval::FuncOp::create(b, loc, fn_name, fn_type, "",
                                           false, false, false, nullptr);

    auto* entry = new mlir::Block();
    fn.getBody().push_back(entry);
    llvm::SmallVector<mlir::Location> arg_locs(param_types.size(), loc);
    entry->addArguments(param_types, arg_locs);
    b.setInsertionPointToStart(entry);

    ctx.push_scope();

    // Bind self.
    ctx.define("__this", entry->getArgument(0));
    ctx.define("self",   entry->getArgument(0));

    // Bind declared parameters.
    size_t arg_idx = 1;
    for (const auto& param : fn_node->parameters) {
        for (const auto& [pname, _] : param.parameter) {
            if (arg_idx < entry->getNumArguments())
                ctx.define(pname, entry->getArgument(arg_idx));
            ++arg_idx;
        }
    }

    nir_emit_body(fn_node->body, ctx);

    if (entry->empty() || !entry->back().hasTrait<mlir::OpTrait::IsTerminator>()) {
        if (is_void_ret) {
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{});
        } else {
            auto zero = mlir::narval::ConstantOp::create(b, loc, vt,
                b.getI64IntegerAttr(0)).getResult();
            mlir::narval::ReturnOp::create(b, loc, mlir::ValueRange{zero});
        }
    }

    ctx.pop_scope();
}

void ClassStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    // Register the method names first so later call sites can resolve
    // `obj.method()` to the mangled symbol.
    std::vector<std::string> method_names;
    for (const auto& m : methods)
        if (m && m->name != "new") method_names.push_back(m->name);
    ctx.register_class_methods(name, method_names);

    for (const auto& method : methods) {
        if (!method || !method->method_def) continue;
        if (method->method_def->kind != NodeType::FunctionStatement) continue;
        nir_emit_class_method(ctx, name, method->name,
                              static_cast<FunctionStmtNode*>(method->method_def.get()));
    }
}
