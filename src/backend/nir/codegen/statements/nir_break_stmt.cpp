#include "../nir_codegen_utils.hpp"
#include "backend/nir/NarvalOps.h"
#include "frontend/ast/statements/break_stmt_node.hpp"

// narval.break — terminated by LowerNarvalControlFlowPass as a branch out of
// the innermost loop.
void BreakStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto& b   = ctx.get_builder();
    mlir::narval::BreakOp::create(b, loc);
}
