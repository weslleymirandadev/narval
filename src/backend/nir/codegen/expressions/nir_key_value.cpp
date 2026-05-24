#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/key_value_node.hpp"

// KeyValueNode is only used as a child inside MapNode; MapNode iterates
// its properties and calls this directly. Push key then value.
void KeyValueNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    if (key)   key->nir_codegen(ctx);
    if (value) value->nir_codegen(ctx);
}
