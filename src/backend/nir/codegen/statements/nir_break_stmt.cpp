#include "../nir_codegen_utils.hpp"
#include "backend/nir/NarvalOps.h"
#include "frontend/ast/statements/break_stmt_node.hpp"

// narval.break — terminated by LowerNarvalControlFlowPass as a branch out of
// the innermost loop.
void BreakStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto& b   = ctx.get_builder();
    // Hand the loop-carried values as they stand here: when the break sits after an
    // assignment, the iteration that breaks still has work to contribute.
    llvm::SmallVector<mlir::Value> carried;
    for (const std::string& name : ctx.loop_carried_names) {
        if (mlir::Value v = ctx.lookup(name)) carried.push_back(v);
    }
    mlir::narval::BreakOp::create(b, loc, carried);
}
