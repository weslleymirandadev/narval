#include "frontend/ast/expressions/identifier_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/Support/raw_ostream.h>

void IdentifierNode::codegen(nv::IRGenerationContext& context) {
    context.set_debug_location(position.get());
    auto symbol_opt = context.get_symbol_info(symbol);
    if (!symbol_opt) {
        // Intrínseco: 'json' é um objeto especial da linguagem
        if (symbol == "json") {
            auto* I8P = nv::ir_utils::get_i8_ptr(context);
            auto* nullJson = llvm::Constant::getNullValue(I8P);
            context.push_value(nullJson);
            return;
        }
        // Erro: variável não declarada
        llvm::errs() << "Erro: identificador '" << symbol << "' não encontrado.\n";
        context.push_value(nullptr);
        return;
    }

    const nv::SymbolInfo& info = symbol_opt.value();

    // Se for uma alocação (variável local), precisamos carregar o valor
    if (info.is_allocated) {
        auto* v = context.get_builder().CreateLoad(info.llvm_type, info.value, symbol.c_str());
        context.push_value(v);
        return;
    }
    // Caso contrário, é uma constante ou valor direto (ex: parâmetro de função)
    context.push_value(info.value);
}