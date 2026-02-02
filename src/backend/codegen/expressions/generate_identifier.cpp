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

    // Se info.value for nullptr, significa que foi registrado por import mas ainda não foi criado
    // Neste caso, tentar buscar a variável global ou função diretamente no módulo
    // NOTA: Se foi importado com alias, precisamos buscar pelo nome original, não pelo alias
    // Mas não temos acesso ao nome original aqui, então tentamos ambos
    llvm::Value* actual_value = info.value;
    if (!actual_value) {
        auto& M = context.get_module();
        // Tentar buscar pelo símbolo atual (pode ser alias)
        auto* global_var = M.getGlobalVariable(symbol);
        auto* function = M.getFunction(symbol);
        
        // Se não encontrou, pode ser que seja um alias - neste caso, o módulo manager
        // deveria ter resolvido isso, mas vamos tentar buscar de outras formas
        // (por exemplo, procurando em todas as funções/variáveis globais)
        // Na prática, isso não deveria acontecer se o import foi feito corretamente
        
        if (global_var) {
            // Encontrou a variável global, atualizar a entrada na tabela de símbolos
            nv::SymbolInfo updated_info = info;
            updated_info.value = global_var;
            updated_info.llvm_type = nv::ir_utils::get_value_struct(context);
            updated_info.is_constant = false;
            context.get_symbol_table().define_symbol(symbol, updated_info);
            actual_value = global_var;
        } else if (function) {
            // Encontrou a função, atualizar a entrada na tabela de símbolos
            nv::SymbolInfo updated_info = info;
            updated_info.value = function;
            updated_info.llvm_type = function->getType();
            updated_info.is_constant = true;  // funções são constantes
            context.get_symbol_table().define_symbol(symbol, updated_info);
            actual_value = function;
        } else {
            // Variável/função ainda não foi criada, erro
            llvm::errs() << "Erro: identificador '" << symbol << "' não encontrado (importado mas não declarado).\n";
            context.push_value(nullptr);
            return;
        }
    }

    // Se for uma função, retornar diretamente (não precisa carregar)
    if (llvm::isa<llvm::Function>(actual_value)) {
        context.push_value(actual_value);
        return;
    }
    
    // Se for uma alocação (variável local) ou global, precisamos carregar o valor
    if (info.is_allocated) {
        // Variável local (AllocaInst)
        auto* v = context.get_builder().CreateLoad(info.llvm_type, actual_value, symbol.c_str());
        context.push_value(v);
        return;
    } else if (llvm::isa<llvm::GlobalVariable>(actual_value)) {
        // Variável global: carregar o valor do GlobalVariable
        // O GlobalVariable deve estar inicializado corretamente via @llvm.global_ctors
        // com a tag de tipo correta
        auto& B = context.get_builder();
        auto* ValueTy = nv::ir_utils::get_value_struct(context);
        auto* ValuePtr = nv::ir_utils::get_value_ptr(context);
        
        // Carregar o valor
        auto* loaded_value = B.CreateLoad(info.llvm_type, actual_value, symbol.c_str());
        
        // Garantir que o tipo está correto usando ensure_value_type
        // Criar alloca temporário para passar para ensure_value_type
        auto* temp_alloca = B.CreateAlloca(ValueTy, nullptr, symbol + "_ensure");
        B.CreateStore(loaded_value, temp_alloca);
        
        // Chamar ensure_value_type para garantir que a tag está correta
        auto* ensure_func = context.ensure_runtime_func("ensure_value_type", {ValuePtr});
        B.CreateCall(ensure_func, {temp_alloca});
        
        // Carregar o valor garantido
        auto* ensured_value = B.CreateLoad(ValueTy, temp_alloca, symbol + "_ensured");
        context.push_value(ensured_value);
        return;
    }
    // Caso contrário, é uma constante ou valor direto (ex: parâmetro de função)
    context.push_value(actual_value);
}