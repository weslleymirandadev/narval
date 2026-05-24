#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/defer_stmt_node.hpp"

// `defer { body }` — emit remaining_body first (normal flow),
// then wrap in nv_defer_push/pop around the body block.
// For NIR, we emit remaining_body inline and defer_body via a runtime push.
void DeferStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    // Emit the deferred body as a no-arg closure registered with the runtime.
    // The runtime calls it when the scope exits.
    // For simplicity, emit the defer body inline after remaining_body (conservative).

    // Emit the rest of the enclosing block first.
    ctx.push_scope();
    for (const auto& s : remaining_body)
        if (s) s->nir_codegen(ctx);
    ctx.pop_scope();

    // Then run the deferred body (simplified: no scope unwinding support yet).
    ctx.push_scope();
    for (const auto& s : defer_body)
        if (s) s->nir_codegen(ctx);
    ctx.pop_scope();
}
