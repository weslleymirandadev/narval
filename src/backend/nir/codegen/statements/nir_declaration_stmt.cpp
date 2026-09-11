#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"

void DeclarationStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());

    mlir::Value rhs;
    if (value) {
        value->nir_codegen(ctx);
        rhs = ctx.pop_value();
    }
    if (!rhs)
        rhs = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));

    std::string binding;
    if (target && target->kind == NodeType::Identifier)
        binding = static_cast<IdentifierNode*>(target.get())->symbol;

    if (!binding.empty()) {
        ctx.define(binding, rhs);
        if (ctx.is_repl_global(binding)) {
            // In the REPL this declaration is the line that gave the variable its
            // value; the following inputs get a fresh JIT, so keep it in the runtime
            // store (nv_repl_get reads it back there).
            auto& b  = ctx.get_builder();
            auto  vt = ctx.get_narval_value_type();
            ctx.ensure_runtime_func("nv_repl_set",
                mlir::FunctionType::get(&ctx.get_mlir_context(), {vt, vt}, {vt}));
            mlir::Value name_val = nir_emit_const(ctx, loc, b.getStringAttr(binding));
            mlir::narval::CallRuntimeOp::create(
                b, loc, mlir::TypeRange{vt},
                mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_repl_set"),
                mlir::ValueRange{name_val, rhs});
        }
    }
}
