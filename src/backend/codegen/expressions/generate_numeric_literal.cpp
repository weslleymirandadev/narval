#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include "backend/codegen/generate_ir.hpp"

void NumericLiteralNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    
    // Registrar features usadas
    nv::register_feature("str");
    nv::register_feature("string_operations");
    
    // Heurística simples: contém ponto → float; senão int
    if (value.find('.') != std::string::npos) {
        double dbl = std::stod(value);
        ctx.push_value(nv::ir_utils::create_float_constant(ctx, dbl));
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
    ctx.push_value(nv::ir_utils::create_int_constant(ctx, integer));
}
