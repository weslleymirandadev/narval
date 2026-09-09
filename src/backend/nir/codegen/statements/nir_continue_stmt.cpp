#include "../nir_codegen_utils.hpp"
#include "backend/nir/NarvalOps.h"
#include "frontend/ast/statements/continue_stmt_node.hpp"

// narval.continue — terminated by LowerNarvalControlFlowPass as a branch back
// to the condition of the innermost loop.
void ContinueStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto& b   = ctx.get_builder();
    mlir::narval::ContinueOp::create(b, loc);
}
