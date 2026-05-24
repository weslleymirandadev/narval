#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/import_stmt_node.hpp"

// Imports are resolved at parse/check time; no codegen needed for NIR.
void ImportStmtNode::nir_codegen(nv::NIRGenerationContext& ctx) {}
