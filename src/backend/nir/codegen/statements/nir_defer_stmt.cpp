#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/defer_stmt_node.hpp"

// `defer { body }` — the parser hands the node the rest of its block
// (remaining_body) and the deferred statements (defer_body). The runtime has no
// scope unwinding, so the deferred body is emitted once the rest of the block has
// been emitted.
//
// The one case that needs care is a `return` in the rest of the block: emitting
// the deferred body after the return put ops behind a terminator and the module
// failed to lower ("Terminator found in the middle of a basic block"). The
// deferred statements run BEFORE the return instead, which is what "runs when the
// block finishes" means.
void DeferStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto emit_deferred = [&]() {
        ctx.push_scope();
        for (const auto& s : defer_body)
            if (s) s->nir_codegen(ctx);
        ctx.pop_scope();
    };

    bool deferred_done = false;
    for (const auto& s : remaining_body) {
        if (!s) continue;
        if (!deferred_done && s->kind == NodeType::ReturnStatement) {
            emit_deferred();
            deferred_done = true;
        }
        s->nir_codegen(ctx);
    }

    if (!deferred_done) emit_deferred();
}
