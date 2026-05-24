#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "mlir/Dialect/Func/IR/FuncOps.h"

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
    for (size_t i = 0; i < cases.size() && i < bodies.size(); ++i) {
        cases[i]->nir_codegen(ctx);
        mlir::Value case_val = ctx.pop_value();

        mlir::Value matched = mlir::func::CallOp::create(b, loc, eq_fn,
            {subject, case_val}).getResult(0);

        auto if_op = ctx.emit_if(loc, matched, {});

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
}
