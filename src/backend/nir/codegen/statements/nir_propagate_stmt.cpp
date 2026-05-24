#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/propagate_stmt_node.hpp"

// `propagate` re-throws the last error in a fallible function.
// Emits a call to nv_propagate which internally calls nv_throw with the pending error.
void PropagateStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto loc = ctx.loc(position.get());
    nir_call_runtime(ctx, loc, "nv_propagate", {}, {});
}
