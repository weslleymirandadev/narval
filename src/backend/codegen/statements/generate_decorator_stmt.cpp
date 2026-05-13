#include "frontend/ast/statements/decorator_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"

// O processamento real dos decorators é feito no pré-scan de generate_ir.cpp,
// antes de declare_runtime(). Este codegen é um no-op intencional.
void DecoratorStmtNode::codegen(nv::IRGenerationContext& ctx) {}
