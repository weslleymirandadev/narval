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
        std::string var_name = item.alias.empty() ? item.name : item.alias;  // Nome usado no código (alias ou original)
        std::string original_name = item.name;  // Nome original no módulo exportado
        
        // Verificar se já está registrado na tabela de símbolos (usando o alias/nome usado)
        auto existing_info = ctx.get_symbol_table().lookup_symbol(var_name);
        
        // IMPORTANTE: Buscar pelo nome original no módulo, não pelo alias
        // A função/variável foi criada com o nome original, não com o alias
        auto* existing_global = M.getGlobalVariable(original_name);
        
        // Verificar se a função já existe no módulo (pode ter sido criada pela declaração real)
        auto* existing_function = M.getFunction(original_name);
        
        if (existing_global) {
            // Variável global já existe (criada pela declaração real com o nome original)
            // Registrar na tabela de símbolos com o alias/nome usado (var_name)
            // Isso permite que o código use o alias para acessar a variável original
            if (existing_info.has_value() && existing_info.value().value == existing_global) {
                // Já está registrado corretamente, não fazer nada
                continue;
            }
            // Registrar/atualizar na tabela de símbolos com a variável global existente
            // IMPORTANTE: Registramos com var_name (alias se houver, ou nome original)
            nv::SymbolInfo info(
                existing_global,  // Aponta para a variável criada com nome original
                ValueTy,
                nullptr,  // tipo Narval desconhecido para imports
                false,    // não é alocação local
                false     // não é constante
            );
            ctx.get_symbol_table().define_symbol(var_name, info);  // Registra com alias
            continue;
        } else if (existing_function) {
            // Função já existe (criada pela declaração real com o nome original)
            // Registrar na tabela de símbolos com o alias/nome usado (var_name)
            // Isso permite que o código use o alias para acessar a função original
            if (existing_info.has_value() && existing_info.value().value == existing_function) {
                // Já está registrado corretamente, não fazer nada
                continue;
            }
            // Registrar/atualizar na tabela de símbolos com a função existente
            // IMPORTANTE: Registramos com var_name (alias se houver, ou nome original)
            nv::SymbolInfo info(
                existing_function,  // Aponta para a função criada com nome original
                existing_function->getType(),
                nullptr,  // tipo Narval desconhecido para imports
                false,    // não é alocação local
                true      // é constante (funções são constantes)
            );
            ctx.get_symbol_table().define_symbol(var_name, info);  // Registra com alias
            continue;
        }
        
        // Variável/função ainda não foi criada
        if (existing_info.has_value() && existing_info.value().value != nullptr) {
            // Já está registrado com um valor não-null, não sobrescrever
            continue;
        }
        
        // IMPORTANTE: Não criar a variável global ou função aqui!
        // Como os módulos são combinados em um único módulo LLVM,
        // a declaração real (no módulo exportado) será processada
        // e criará a variável global ou função com o valor correto.
        // O import apenas registra na tabela de símbolos que será criado depois.
        // Usamos um placeholder nullptr que será substituído quando a declaração for processada.
        
        // Registrar na tabela de símbolos com nullptr temporário apenas se não estiver registrado
        // Quando a declaração real for processada, ela será criada com o nome original (original_name)
        // Então precisamos buscar pelo nome original quando tentarmos resolver o alias
        if (!existing_info.has_value()) {
            // Não sabemos ainda se é variável ou função, então usamos ValueTy como tipo padrão
            // Será atualizado quando a declaração real for processada
            // IMPORTANTE: Armazenamos o nome original para poder buscar depois
            // Mas como SymbolInfo não tem campo para isso, vamos tentar buscar pelo nome original
            // quando necessário (isso será feito em generate_identifier.cpp)
            nv::SymbolInfo info(
                nullptr,  // será atualizado quando a declaração real for processada
                ValueTy,  // tipo temporário, será atualizado
                nullptr,  // tipo Narval desconhecido para imports
                false,    // não é alocação local
                false     // não é constante (será atualizado)
            );
            ctx.get_symbol_table().define_symbol(var_name, info);
            
            // Também tentar buscar pelo nome original agora (pode ter sido criado em outra ordem)
            // Se encontrar, atualizar imediatamente
            auto* check_global = M.getGlobalVariable(original_name);
            auto* check_function = M.getFunction(original_name);
            if (check_global) {
                info.value = check_global;
                info.llvm_type = ValueTy;
                info.is_constant = false;
                ctx.get_symbol_table().define_symbol(var_name, info);
            } else if (check_function) {
                info.value = check_function;
                info.llvm_type = check_function->getType();
                info.is_constant = true;
                ctx.get_symbol_table().define_symbol(var_name, info);
            }
        }
    }
}
