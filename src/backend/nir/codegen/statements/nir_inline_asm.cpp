#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/inline_asm_stmt_node.hpp"

// Inline assembly is only available in @[abi] functions that lower directly
// through the legacy LLVM IR path. No-op for the NIR pipeline.
void InlineAsmStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {}
