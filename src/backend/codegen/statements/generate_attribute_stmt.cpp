#include "frontend/ast/statements/attribute_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"

// O processamento real dos atributos é feito no pré-scan de generate_ir.cpp,
// antes de declare_runtime(). Este codegen é um no-op intencional.
void AttributeStmtNode::codegen(nv::IRGenerationContext& ctx) {}
