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

    // Prefixos de base: 0b..., 0o..., 0x...
    if (value.size() >= 2 && value[0] == '0') {
        char prefix = value[1];
        switch (prefix) {
            case 'b':
                base = 2;
                break;
            case 'o':
                base = 8;
                break;
            case 'x':
                base = 16;
                break;
            default:
                break;
        }
        if (base != 10) {
            value = value.substr(2);
        }
    }

    int32_t integer = std::stoi(value, nullptr, base);
    context.push_value(rph::ir_utils::create_int_constant(context, integer));
}



