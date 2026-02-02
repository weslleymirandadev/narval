#include "frontend/interactive/incremental_ir_builder.hpp"
#include "frontend/interactive/session_manager.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "frontend/ast/program.hpp"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/Casting.h>

namespace nv {
namespace interactive {

IncrementalIRBuilder::IncrementalIRBuilder(llvm::LLVMContext& llvm_context)
    : llvm_context(llvm_context), session_manager(nullptr) {
    // Criar módulo principal da sessão
    session_module = std::make_unique<llvm::Module>("narval_session", llvm_context);
}

std::unique_ptr<llvm::Module> IncrementalIRBuilder::build_unit_ir(ExecutionUnit& unit, Checker& checker) {
    // Criar módulo para esta unidade
    auto unit_module = create_unit_module(unit.get_id(), unit.get_source_name());
    
    // Criar IRBuilder para este módulo
    llvm::IRBuilder<llvm::NoFolder> builder(llvm_context);
    
    // Criar contexto de geração de IR
    IRGenerationContext ir_context(llvm_context, *unit_module, builder, &checker);
    
    // Configurar arquivo fonte para debug
    ir_context.set_source_file(unit.get_source_name());
    
    // Gerar IR para o AST da unidade
    // O parser já retorna um Program, então podemos usar generate_ir diretamente
    Node* ast = unit.get_ast();
    if (ast && ast->kind == NodeType::Program) {
        // Criar unique_ptr temporário para passar para generate_ir
        // Nota: generate_ir espera ownership, mas não podemos transferir
        // Vamos clonar o AST para gerar IR
        std::unique_ptr<Node> ast_clone(ast->clone());
        
        // Gerar IR
        try {
            generate_ir(std::move(ast_clone), ir_context);
        } catch (...) {
            // Erro na geração de IR
            return nullptr;
        }
    } else {
        // AST não é um Program - isso não deveria acontecer se o parser está correto
        // Mas vamos tratar como erro
        return nullptr;
    }
    
    // Resolver referências a símbolos já compilados
    if (session_manager) {
        resolve_symbol_references(*unit_module, unit);
    }
    
    // Marcar como válido
    valid_units.insert(unit.get_id());
    unit_modules[unit.get_id()] = std::move(unit_module);
    
    return std::move(unit_modules[unit.get_id()]);
}

void IncrementalIRBuilder::invalidate_unit_ir(ExecutionUnitId unit_id) {
    valid_units.erase(unit_id);
    // Não removemos o módulo imediatamente - pode ser útil para debug
    // Mas marcamos como inválido
}

llvm::Module* IncrementalIRBuilder::get_unit_module(ExecutionUnitId unit_id) const {
    auto it = unit_modules.find(unit_id);
    if (it != unit_modules.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool IncrementalIRBuilder::has_valid_ir(ExecutionUnitId unit_id) const {
    return valid_units.find(unit_id) != valid_units.end();
}

std::unique_ptr<llvm::Module> IncrementalIRBuilder::create_unit_module(ExecutionUnitId unit_id, const std::string& source_name) {
    std::string module_name = "narval_unit_" + std::to_string(unit_id);
    return std::make_unique<llvm::Module>(module_name, llvm_context);
}

void IncrementalIRBuilder::resolve_symbol_references(llvm::Module& unit_module, ExecutionUnit& unit) {
    // Resolver referências a símbolos já compilados no módulo da sessão
    
    if (!session_manager) {
        return;
    }
    
    // Para cada símbolo usado pela unidade que não é definido nela,
    // criar declaração externa se o símbolo já existe no módulo da sessão
    for (const auto& symbol_name : unit.get_used_symbols()) {
        // Se o símbolo é definido nesta unidade, não precisa de declaração externa
        if (unit.get_defined_symbols().find(symbol_name) != unit.get_defined_symbols().end()) {
            continue;
        }
        
        // Verificar se o símbolo existe na sessão
        auto* symbol_info = session_manager->lookup_symbol(symbol_name);
        if (!symbol_info) {
            continue;  // Símbolo não existe ainda - será resolvido em runtime ou erro será reportado
        }
        
        // Verificar se o símbolo já está no módulo da sessão
        llvm::GlobalValue* existing_global = session_module->getNamedValue(symbol_name);
        if (existing_global) {
            // Criar declaração externa no módulo da unidade
            // O LLVM JIT resolverá isso durante o linking
            // Por enquanto, apenas garantir que o símbolo está acessível
            if (!unit_module.getNamedValue(symbol_name)) {
                // Criar declaração externa com o mesmo tipo
                llvm::GlobalValue::LinkageTypes linkage = llvm::GlobalValue::ExternalLinkage;
                llvm::Type* type = existing_global->getValueType();
                
                // Verificar se é uma função usando isa<> ou dyn_cast
                if (llvm::isa<llvm::Function>(existing_global)) {
                    auto* func = llvm::cast<llvm::Function>(existing_global);
                    auto* func_type = func->getFunctionType();
                    unit_module.getOrInsertFunction(symbol_name, func_type, linkage);
                } else {
                    // Variável global
                    unit_module.getOrInsertGlobal(symbol_name, type);
                }
            }
        }
    }
}

void IncrementalIRBuilder::clear_unit_modules() {
    // Limpar todos os módulos de unidades, mantendo apenas o módulo da sessão
    unit_modules.clear();
    valid_units.clear();
}

} // namespace interactive
} // namespace nv
