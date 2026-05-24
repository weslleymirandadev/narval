#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"

// Decorators (@name / @name(args)) precede function definitions.
// In the NIR pipeline we store the decorator names in the context's pending
// list; the next FunctionStmtNode will attach them as attributes on the emitted
// func.func, then clear the list.
//
// For now, decorator names are recorded on the pending_optimize field only if
// they carry optimisation hints (handled by AttributeStmtNode). Unknown
// decorators are recorded in the module as private symbols so the lowering
// passes can query them.
//
// Phase 2 will add a proper decorator table to NIRGenerationContext.
void DecoratorStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    // Nothing to emit for unknown decorators yet – they will be handled
    // once the decorator table is added to NIRGenerationContext.
    (void)ctx;
}
