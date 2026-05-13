#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"
#include <algorithm>

void NumericLiteralNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    
    nv::register_feature("str");
    nv::register_feature("string_operations");

    std::string literal = value;
    literal.erase(std::remove(literal.begin(), literal.end(), '_'), literal.end());
    
    if (literal.find('.') != std::string::npos ||
        literal.find('e') != std::string::npos ||
        literal.find('E') != std::string::npos) {
        double dbl = std::stod(literal);
        ctx.push_value(nv::ir_utils::create_float_constant(ctx, dbl));
        return;
    }

    int base = 10;
    size_t prefix_index = (!literal.empty() && literal[0] == '-') ? 1 : 0;

    if (literal.size() >= prefix_index + 2 && literal[prefix_index] == '0') {
        char prefix = literal[prefix_index + 1];
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
            literal.erase(prefix_index, 2);
        }
    }

    int32_t integer = std::stoi(literal, nullptr, base);
    ctx.push_value(nv::ir_utils::create_int_constant(ctx, integer));
}
