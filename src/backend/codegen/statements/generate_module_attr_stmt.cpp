#include "frontend/ast/statements/module_attr_node.hpp"
#include "backend/codegen/ir_context.hpp"

// O processamento real dos atributos é feito no pré-scan de generate_ir.cpp,
// antes de declare_runtime(). Este codegen é um no-op intencional.
void ModuleAttrNode::codegen(nv::IRGenerationContext& ctx) {}
