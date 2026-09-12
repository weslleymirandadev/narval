#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/tuple_expr_node.hpp"

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
        } else if (target->kind == NodeType::AccessExpression) {
            // a[i] = rhs → nv_container_set(container, index, rhs). The generic store
            // dispatches on the receiver: a map is keyed by string, an array/vector by
            // integer index. Calling nv_array_set here read a map's string key as an
            // integer, so `m["k"] = v` wrote to a garbage slot and the next read of
            // "k" came back empty.
            //
            // The incref is not decoration: the statement's own reference to the value
            // is released right after this, so without it the container is left holding
            // a pointer to memory that was just freed, and the next read segfaults. That
            // was the whole crash — the runtime's indexed store was fine all along (a
            // direct C test round-trips), and no_std never showed it because nv_drop is a
            // no-op in the freestanding runtime.
            auto* acc = static_cast<AccessExprNode*>(target.get());
            mlir::Value container, index;
            if (acc->expr)  { acc->expr->nir_codegen(ctx);  container = ctx.pop_value(); }
            // `t[i, j] = v` on a tensor: one row-major offset, then the ordinary flat store.
            // Twin of the read path in nir_access_expr.cpp; keep the two in step.
            if (acc->tensor_multi_index && acc->index &&
                acc->index->kind == NodeType::TupleExpression) {
                auto* tup = static_cast<TupleExprNode*>(acc->index.get());
                std::vector<mlir::Value> args;
                args.push_back(container);
                args.push_back(nir_emit_const(ctx, loc,
                    ctx.get_builder().getI64IntegerAttr((int64_t)tup->elements.size())));
                for (auto& el : tup->elements) {
                    el->nir_codegen(ctx);
                    args.push_back(ctx.has_value() ? ctx.pop_value() : mlir::Value{});
                }
                while (args.size() < 6) {
                    args.push_back(nir_emit_const(ctx, loc,
                        ctx.get_builder().getI64IntegerAttr(0)));
                }
                index = nir_call_runtime(ctx, loc, "nv_tensor_flat_index", args, {vt});
            } else if (acc->index) {
                acc->index->nir_codegen(ctx);
                index = ctx.pop_value();
            }
            if (container && index) {
                ctx.ensure_runtime_func("nv_incref_bridge",
                    mlir::FunctionType::get(&ctx.get_mlir_context(), {vt}, {vt}));
                nir_call_runtime(ctx, loc, "nv_incref_bridge", {rhs}, {vt});
                nir_call_runtime(ctx, loc, "nv_container_set", {container, index, rhs}, {});
            }
        } else {
            // Other lvalue shapes are not assignable yet; evaluating the target keeps
            // its side effects (a call inside it, for instance) observable.
            target->nir_codegen(ctx);
            ctx.pop_value();
        }
    }

    ctx.push_value(rhs);
}
