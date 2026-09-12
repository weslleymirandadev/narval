#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/or_expr_node.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include <functional>

void MatchStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());
    auto  vt  = ctx.get_narval_value_type();
    auto  i1  = b.getI1Type();

    mlir::Value subject;
    if (target) {
        target->nir_codegen(ctx);
        subject = ctx.pop_value();
    }
    if (!subject) return;

    // Runtime equality check returning i1.
    auto eq_fn_t = mlir::FunctionType::get(&ctx.get_mlir_context(), {vt, vt}, {i1});
    ctx.ensure_runtime_func("nv_value_eq_bool", eq_fn_t);
    auto eq_fn = ctx.get_module().lookupSymbol<mlir::func::FuncOp>("nv_value_eq_bool");

    // Emit a chain of if/else for each arm.
    // After building each narval.if, the next arm goes into its else region.
    mlir::Operation* outer_if = nullptr;
    for (size_t i = 0; i < cases.size() && i < bodies.size(); ++i) {
        // `_` is parsed as the identifier "default": its arm matches anything.
        bool is_default_arm = false;
        if (auto* id_node = dynamic_cast<IdentifierNode*>(cases[i].get()))
            is_default_arm = (id_node->symbol == "default");

        mlir::Value matched;
        if (is_default_arm) {
            // The `_` arm matches anything, and no test is emitted for it: resolving the synthetic
            // "default" binding would only produce a "no value bound" warning.
            matched = mlir::arith::ConstantOp::create(b, loc, i1,
                          b.getIntegerAttr(i1, 1)).getResult();
        } else {
            // A pattern may be several alternatives separated by '|'; any match counts.
            std::vector<Expr*> alternatives;
            std::function<void(Expr*)> collect = [&](Expr* e) {
                if (e && e->kind == NodeType::OrExpression) {
                    auto* or_node = static_cast<OrExprNode*>(e);
                    collect(or_node->expr.get());
                    collect(or_node->value_handler.get());
                    return;
                }
                alternatives.push_back(e);
            };
            collect(cases[i].get());

            for (Expr* alternative : alternatives) {
                if (!alternative) continue;
                alternative->nir_codegen(ctx);
                mlir::Value alt_val = ctx.pop_value();
                mlir::Value one = mlir::func::CallOp::create(b, loc, eq_fn,
                    {subject, alt_val}).getResult(0);
                matched = matched ? mlir::arith::OrIOp::create(b, loc, matched, one).getResult()
                                  : one;
            }
            if (!matched) continue;
        }

        auto if_op = ctx.emit_if(loc, matched, {});
        if (!outer_if) outer_if = if_op.getOperation();

        // Then: arm body.
        {
            mlir::OpBuilder::InsertionGuard g(b);
            b.setInsertionPointToStart(&if_op.getThenRegion().front());
            ctx.push_scope();
            for (const auto& s : bodies[i]) if (s) s->nir_codegen(ctx);
            ctx.pop_scope();
            nir_terminate(if_op.getThenRegion().front(), b, loc);
        }

        // Else: seed it with a yield; next iteration will emit into it.
        {
            mlir::OpBuilder::InsertionGuard g(b);
            b.setInsertionPointToStart(&if_op.getElseRegion().front());
            mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{});
        }

        // Continue inserting into the else block for chaining.
        b.setInsertionPointToStart(&if_op.getElseRegion().front());
    }

    if (outer_if)
        b.setInsertionPointAfter(outer_if);
}
