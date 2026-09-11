#include "../nir_codegen_utils.hpp"
#include "llvm/Support/raw_ostream.h"
#include "frontend/ast/expressions/identifier_node.hpp"

IdentifierNode::~IdentifierNode() = default;

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
