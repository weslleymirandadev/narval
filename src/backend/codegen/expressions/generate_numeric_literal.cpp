#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"

void NumericLiteralNode::codegen(rph::IRGenerationContext& context) {
    // Heurística simples: contém ponto → float; senão int
    if (value.find('.') != std::string::npos) {
        double dbl = std::stod(value);
        context.push_value(rph::ir_utils::create_float_constant(context, dbl));
        return;
    }

    int32_t integer = std::stoi(value);
    context.push_value(rph::ir_utils::create_int_constant(context, integer));
}



