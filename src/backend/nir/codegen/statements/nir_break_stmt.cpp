#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/break_stmt_node.hpp"

// narval dialect has no dedicated break op; emit a void yield to terminate
// the current region. The surrounding for_range/while will be restructured
// during lowering — this is a best-effort placeholder.
void BreakStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto& b   = ctx.get_builder();
    mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{});
}
