#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <iostream>

void NumericLiteralNode::codegen(rph::IRGenerationContext& context) {
    // Heurística simples: contém ponto → float; senão int
    if (value.find('.') != std::string::npos) {
        double dbl = std::stod(value);
        context.push_value(rph::ir_utils::create_float_constant(context, dbl));
        return;
    }

    int base = 10;
    switch (value.at(1)) {
        case 'b':
            base = 2;
            break;
        case 'o':
            base = 8;
            break;
        case 'x':
            base = 16;
            break;
    }
    std::cout << value << std::endl;
    if (base != 10) {
        value = value.substr(2, value.size());
    }
    std::cout << value << std::endl;
    int32_t integer = std::stoi(value, 0, base);
    context.push_value(rph::ir_utils::create_int_constant(context, integer));
}



