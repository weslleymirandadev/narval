#include "../nir_codegen_utils.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"

void NumericLiteralNode::nir_codegen(nv::NIRGenerationContext& ctx) {
    auto  loc = ctx.loc(position.get());
    auto& b   = ctx.get_builder();

    bool is_float = value.find('.') != std::string::npos ||
                    value.find('e') != std::string::npos ||
                    value.find('E') != std::string::npos;

    mlir::Attribute attr;
    if (is_float)
        attr = b.getF64FloatAttr(std::stod(value));
    else
        attr = b.getI64IntegerAttr(std::stoll(value));

    ctx.push_value(nir_emit_const(ctx, loc, attr));
}
