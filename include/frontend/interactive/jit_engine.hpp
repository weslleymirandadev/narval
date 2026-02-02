#pragma once

#include "frontend/interactive/execution_unit.hpp"
#include "frontend/interactive/incremental_ir_builder.hpp"
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace nv {
namespace interactive {

/**
 * JIT Execution Engine
 * 
 * Responsável por executar código incrementalmente via LLVM JIT.
 * 
 * Funções:
 * - Compilar IR parcial via LLVM JIT
 * - Executar fragmentos isoladamente
 * - Permitir redefinição de funções e variáveis
 * - Gerenciar símbolos globais da sessão no nível do runtime
 * 
 * Características:
 * - Execução imediata
 * - Baixa latência
 * - Suporte a hot reload
 * - Integração direta com o Session Manager
 */
class JITEngine {
public:
    JITEngine();
    ~JITEngine();
    
    /**
     * Inicializa o JIT engine
     */
    bool initialize();
    
    /**
     * Adiciona um módulo ao JIT para execução
     * 
     * @param module Módulo LLVM a ser adicionado
     * @param unit_id ID da unidade associada
     * @return true se adicionado com sucesso
     */
    bool add_module(std::unique_ptr<llvm::Module> module, ExecutionUnitId unit_id);
    
    /**
     * Remove um módulo do JIT (quando invalidado)
     */
    void remove_module(ExecutionUnitId unit_id);
    
    /**
     * Executa uma função específica
     * 
     * @param function_name Nome da função a executar
     * @param args Argumentos (se houver)
     * @return Valor de retorno (se houver)
     */
    template<typename RetType = void, typename... Args>
    RetType execute_function(const std::string& function_name, Args... args);
    
    /**
     * Executa o código de uma unidade (expressão ou statement)
     * 
     * @param unit_id ID da unidade
     * @return Valor de retorno (se aplicável)
     */
    void* execute_unit(ExecutionUnitId unit_id);
    
    /**
     * Obtém um ponteiro para um símbolo global
     */
    void* get_symbol_address(const std::string& symbol_name);
    
    /**
     * Verifica se o JIT está inicializado
     */
    bool is_initialized() const { return jit != nullptr; }
    
    /**
     * Limpa todos os módulos do JIT
     */
    void clear();
    
    /**
     * Obtém todas as unidades registradas
     */
    std::unordered_set<ExecutionUnitId> get_registered_units() const;

private:
    std::unique_ptr<llvm::orc::LLJIT> jit;
    
    // Mapeamento: unit_id -> módulo no JIT
    std::unordered_map<ExecutionUnitId, llvm::orc::ResourceTrackerSP> unit_trackers;
    
    // Mapeamento: unit_id -> nome da função wrapper gerada
    std::unordered_map<ExecutionUnitId, std::string> unit_wrapper_functions;
    
    /**
     * Cria um ThreadSafeModule a partir de um Module usando o contexto do módulo
     */
    llvm::orc::ThreadSafeModule create_thread_safe_module(std::unique_ptr<llvm::Module> module);
    
    /**
     * Gera uma função wrapper para executar o código de uma unidade
     */
    std::string generate_wrapper_function(llvm::Module& module, ExecutionUnitId unit_id);
};

} // namespace interactive
} // namespace nv
