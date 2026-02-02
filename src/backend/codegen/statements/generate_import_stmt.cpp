#include "frontend/ast/statements/import_stmt_node.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <llvm/IR/GlobalVariable.h>

void ImportStmtNode::codegen(nv::IRGenerationContext& ctx) {
    ctx.set_debug_location(position.get());
    
    // Para cada identificador importado, criar uma declaração externa
    // da variável global correspondente
    auto& M = ctx.get_module();
    auto* ValueTy = nv::ir_utils::get_value_struct(ctx);
    
    for (const auto& item : imports) {
        std::string var_name = item.alias.empty() ? item.name : item.alias;
        
        // Verificar se a variável já existe no módulo (pode ter sido criada pela declaração real)
        auto* existing_global = M.getGlobalVariable(var_name);
        
        // Verificar se já está registrado na tabela de símbolos
        auto existing_info = ctx.get_symbol_table().lookup_symbol(var_name);
        
        if (existing_global) {
            // Variável global já existe (criada pela declaração real)
            if (existing_info.has_value() && existing_info.value().value == existing_global) {
                // Já está registrado corretamente, não fazer nada
                continue;
            }
            // Registrar/atualizar na tabela de símbolos com a variável global existente
            nv::SymbolInfo info(
                existing_global,
                ValueTy,
                nullptr,  // tipo Narval desconhecido para imports
                false,    // não é alocação local
                false     // não é constante
            );
            ctx.get_symbol_table().define_symbol(var_name, info);
            continue;
        }
        
        // Variável ainda não foi criada
        if (existing_info.has_value() && existing_info.value().value != nullptr) {
            // Já está registrado com um valor não-null, não sobrescrever
            continue;
        }
        
        // IMPORTANTE: Não criar a variável global aqui!
        // Como os módulos são combinados em um único módulo LLVM,
        // a declaração real da variável (no módulo exportado) será processada
        // e criará a variável global com o valor correto.
        // O import apenas registra na tabela de símbolos que a variável será criada depois.
        // Usamos um placeholder nullptr que será substituído quando a declaração for processada.
        
        // Registrar na tabela de símbolos com nullptr temporário apenas se não estiver registrado
        if (!existing_info.has_value()) {
            nv::SymbolInfo info(
                nullptr,  // será atualizado quando a declaração real for processada
                ValueTy,
                nullptr,  // tipo Narval desconhecido para imports
                false,    // não é alocação local
                false     // não é constante
            );
            ctx.get_symbol_table().define_symbol(var_name, info);
        }
    }
}
