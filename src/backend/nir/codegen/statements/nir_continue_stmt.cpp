#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/continue_stmt_node.hpp"

// Similarly to break, emit a void yield as a placeholder continue.
void ContinueStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto& b   = ctx.get_builder();
    mlir::narval::YieldOp::create(b, loc, mlir::ValueRange{});
}
