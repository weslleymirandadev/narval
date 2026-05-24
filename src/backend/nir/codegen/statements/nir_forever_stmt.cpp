#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/forever_stmt_node.hpp"
#include "mlir/Dialect/Arith/IR/Arith.h"

// `forever { body }` — infinite loop; lowers to narval.while(true).
void ForeverStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto& b   = ctx.get_builder();
    auto  loc = ctx.loc(position.get());

    // narval.while takes a condition_region (yields i1) and a body_region.
    auto while_op = ctx.emit_while(loc, {}, {});

    // condition_region: always yield true (i1 = 1)
    {
        mlir::OpBuilder::InsertionGuard g(b);
        b.setInsertionPointToStart(&while_op.getConditionRegion().front());
        auto i1_true = mlir::arith::ConstantIntOp::create(b, loc, 1, 1).getResult();
        mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{i1_true});
    }

    // body_region: emit loop body, then yield to continue
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
