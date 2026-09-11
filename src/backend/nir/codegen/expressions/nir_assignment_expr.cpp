#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"

void AssignmentExprNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();

    // Evaluate right-hand side
    if (value) value->nir_codegen(ctx);
    mlir::Value rhs = ctx.pop_value();
    if (!rhs) rhs = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));

    // Compound assignment: read old value, apply op, produce new rhs
    if (op != "=" && !op.empty()) {
        llvm::StringRef fn;
        if      (op == "+=") fn = "nv_add";
        else if (op == "-=") fn = "nv_sub";
        else if (op == "*=") fn = "nv_mul";
        else if (op == "/=") fn = "nv_div";
        else if (op == "%=") fn = "nv_mod";
        else                 fn = "nv_add";

        mlir::Value lhs_val;
        if (target) {
            target->nir_codegen(ctx);
            lhs_val = ctx.pop_value();
        }
        if (!lhs_val) lhs_val = nir_emit_const(ctx, loc, ctx.get_builder().getI64IntegerAttr(0));
        rhs = nir_call_runtime(ctx, loc, fn, {lhs_val, rhs}, {vt});
    }

    // Write back
    if (target) {
        if (target->kind == NodeType::Identifier) {
            // Simple variable assignment: redefine in current scope
            auto name = static_cast<IdentifierNode*>(target.get())->symbol;
            ctx.define(name, rhs);
            if (ctx.is_repl_global(name)) {
                // Keep it for the following REPL inputs, which get a fresh JIT.
                auto& b   = ctx.get_builder();
                auto  vt  = ctx.get_narval_value_type();
                ctx.ensure_runtime_func("nv_repl_set",
                    mlir::FunctionType::get(&ctx.get_mlir_context(), {vt, vt}, {vt}));
                mlir::Value name_val = nir_emit_const(ctx, loc, b.getStringAttr(name));
                auto set = mlir::narval::CallRuntimeOp::create(
                    b, loc, mlir::TypeRange{vt},
                    mlir::SymbolRefAttr::get(&ctx.get_mlir_context(), "nv_repl_set"),
                    mlir::ValueRange{name_val, rhs});
                rhs = set.getResults()[0];
            }
        } else if (target->kind == NodeType::MemberExpression) {
            // obj.field = rhs  →  narval.set_field
            auto* mem = static_cast<MemberExprNode*>(target.get());
            mem->object->nir_codegen(ctx);
            mlir::Value obj = ctx.pop_value();
            std::string field_name;
            if (mem->property && mem->property->kind == NodeType::Identifier)
                field_name = static_cast<IdentifierNode*>(mem->property.get())->symbol;
            if (!obj || field_name.empty()) { ctx.push_value(rhs); return; }
            mlir::narval::SetFieldOp::create(
                ctx.get_builder(), loc, obj,
                mlir::StringAttr::get(&ctx.get_mlir_context(), field_name), rhs);
        } else if (target->kind == NodeType::AccessExpression && !nir_no_std_entry().empty()) {
            // NOTE: only wired for @[no_std] so far. The same call crashes against the
            // std runtime (its collections keep a reference discipline nv_container_set
            // does not follow yet), and silently doing nothing — as it did before — is
            // less bad than crashing. Remove the gate once that is understood.
            // a[i] = rhs  →  nv_container_set(container, index, rhs)
            auto* acc = static_cast<AccessExprNode*>(target.get());
            mlir::Value container, index;
            if (acc->expr)  { acc->expr->nir_codegen(ctx);  container = ctx.pop_value(); }
            if (acc->index) { acc->index->nir_codegen(ctx); index     = ctx.pop_value(); }
            if (container && index)
                nir_call_runtime(ctx, loc, "nv_container_set", {container, index, rhs}, {});
        } else {
            // Other lvalue shapes are not assignable yet; evaluating the target keeps
            // its side effects (a call inside it, for instance) observable.
            target->nir_codegen(ctx);
            ctx.pop_value();
        }
    }

    ctx.push_value(rhs);
}
