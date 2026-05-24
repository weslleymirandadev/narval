#include "../nir_codegen_utils.hpp"
#include "frontend/ast/statements/module_attr_node.hpp"

// Module attributes (@[no_std], etc.) affect compilation flags but have
// no direct MLIR emission. No-op for NIR.
void ModuleAttrNode::nir_codegen(nv::NIRGenerationContext& ctx) {}
