#include "../nir_codegen_utils.hpp"
#include "llvm/Support/raw_ostream.h"
#include "frontend/ast/expressions/identifier_node.hpp"

// The destructor moved to src/frontend/ast_node_anchors.cpp: it is the key function of this
// class' vtable, and keeping the table in the front-end is what lets the language server link
// without the NIR/MLIR back-end.
void IdentifierNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    mlir::Value v = ctx.lookup(symbol);
    if (!v && ctx.is_repl_global(symbol)) {
        // Assigned on an earlier REPL line: the value is in the runtime store, not in
        // this module (each REPL input is JIT'd into a fresh JIT).
        auto& b   = ctx.get_builder();
        auto  loc = ctx.loc(position.get());
        auto  vt  = ctx.get_narval_value_type();
        ctx.ensure_runtime_func("nv_repl_get",
            mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {vt}));
        mlir::Value name_val = nir_emit_const(ctx, loc, b.getStringAttr(symbol));
        auto call = mlir::narval::CallRuntimeOp::create(
            b, loc, mlir::TypeRange{vt},
            mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_repl_get"),
            mlir::ValueRange{name_val});
        v = call.getResults()[0];
    }
    if (v && ctx.is_module_global(symbol) && ctx.value_outside_region(v)) {
        // Inside a function body, and the name is a module-scope binding: the value belongs
        // to the module's region and cannot be used here. Read it from the runtime store,
        // where the top-level declaration put it.
        auto& b   = ctx.get_builder();
        auto  loc = ctx.loc(position.get());
        auto  vt  = ctx.get_narval_value_type();
        ctx.ensure_runtime_func("nv_global_get",
            mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {vt}));
        mlir::Value name_val = nir_emit_const(ctx, loc, b.getStringAttr(symbol));
        auto call = mlir::narval::CallRuntimeOp::create(
            b, loc, mlir::TypeRange{vt},
            mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_global_get"),
            mlir::ValueRange{name_val});
        v = call.getResults()[0];
    }
    if (!v) {
        // The type checker accepted the name but codegen has no binding for it at
        // this point - typically the first assignment sits inside a nested block
        // (a branch), whose scope ends with the block. Say so instead of quietly
        // substituting 0, which turns a scoping mistake into a wrong answer.
        llvm::errs() << ctx.loc(position.get())
                     << "warning: no value bound for '" << symbol
                     << "' here; it is only assigned inside a nested block. Using 0\n";
        v = nir_emit_const(ctx, ctx.loc(position.get()),
                           ctx.get_builder().getI64IntegerAttr(0));
    }
    ctx.push_value(v);
}
