#pragma once

#include "frontend/interactive/execution_unit.hpp"
#include "frontend/interactive/session_manager.hpp"
#include "backend/codegen/ir_context.hpp"
#include "frontend/checker/checker.hpp"
#include <llvm/IR/Module.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace nv {
namespace interactive {

/**
 * IR Incremental Builder
 * 
 * Responsável por gerar IR parcial para cada unidade interativa.
 * 
 * Funções:
 * - Converter fragmentos analisados em IR independente
 * - Resolver referências a símbolos já compilados
 * - Permitir linkage incremental entre fragmentos
 * - Marcar fragmentos como ativos ou inválidos
 * 
 * Este IR NÃO é final.
 * Ele é projetado para execução imediata via JIT.
 */
class IncrementalIRBuilder {
public:
    IncrementalIRBuilder(llvm::LLVMContext& llvm_context);
    ~IncrementalIRBuilder() = default;
    
    /**
     * Define o SessionManager (para resolução de símbolos)
     */
    void set_session_manager(SessionManager* sm) { session_manager = sm; }
    
    /**
     * Gera IR para uma unidade de execução
     * 
     * @param unit Unidade a ser compilada
     * @param checker Checker com análise semântica completa
     * @return Módulo LLVM com IR gerado
     */
    std::unique_ptr<llvm::Module> build_unit_ir(ExecutionUnit& unit, Checker& checker);
    
    /**
     * Invalida o IR de uma unidade (marca como obsoleto)
     */
    void invalidate_unit_ir(ExecutionUnitId unit_id);
    
    /**
     * Obtém o módulo LLVM de uma unidade
     */
    llvm::Module* get_unit_module(ExecutionUnitId unit_id) const;
    
    /**
     * Verifica se uma unidade tem IR válido
     */
    bool has_valid_ir(ExecutionUnitId unit_id) const;
    
    /**
     * Obtém o módulo principal da sessão (onde símbolos globais são mantidos)
     */
    llvm::Module& get_session_module() { return *session_module; }
    
    /**
     * Limpa todos os módulos de unidades (mantém apenas o módulo da sessão)
     */
    void clear_unit_modules();
    
    /**
     * Obtém todas as unidades com IR válido
     */
    std::unordered_set<ExecutionUnitId> get_valid_units() const { return valid_units; }

private:
    llvm::LLVMContext& llvm_context;
    
    // Módulo principal da sessão (mantém símbolos globais)
    std::unique_ptr<llvm::Module> session_module;
    
    // Módulos por unidade (IR incremental)
    std::unordered_map<ExecutionUnitId, std::unique_ptr<llvm::Module>> unit_modules;
    
    // Set de unidades com IR válido
    std::unordered_set<ExecutionUnitId> valid_units;
    
    /**
     * Cria um novo módulo para uma unidade
     */
    std::unique_ptr<llvm::Module> create_unit_module(ExecutionUnitId unit_id, const std::string& source_name);
    
    /**
     * Resolve referências a símbolos já compilados
     */
    void resolve_symbol_references(llvm::Module& unit_module, ExecutionUnit& unit);
    
    // Referência ao session manager (para resolução de símbolos)
    SessionManager* session_manager;
};

} // namespace interactive
} // namespace nv
