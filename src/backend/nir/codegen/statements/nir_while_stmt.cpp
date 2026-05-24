#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/while_stmt_node.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"

void WhileStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());

    auto while_op = ctx.emit_while(loc, {}, {});

    // Condition region — terminates with narval.yield {i1}.
    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&while_op.getConditionRegion().front());

        mlir::Value i1_cond;
        if (condition) {
            condition->nir_codegen(ctx);
            mlir::Value cond_val = ctx.pop_value();
            if (cond_val) {
                i1_cond = nir_to_i1(ctx, loc, cond_val);
            }
        }
        if (!i1_cond)
            i1_cond = mlir::arith::ConstantIntOp::create(b, loc, 1, 1).getResult();

        mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{i1_cond});
    }

    // Body region.
    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&while_op.getBodyRegion().front());
        ctx.push_scope();
        nir_emit_body(body, ctx);
        ctx.pop_scope();
        nir_terminate(while_op.getBodyRegion().front(), b, loc);
    }

    b.setInsertionPointAfter(while_op);
}
