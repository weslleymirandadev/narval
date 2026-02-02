#pragma once

#include "frontend/interactive/session_manager.hpp"
#include "frontend/interactive/execution_unit.hpp"
#include "frontend/interactive/incremental_checker.hpp"
#include "frontend/interactive/epoch_system.hpp"
#include "frontend/interactive/incremental_ir_builder.hpp"
#include "frontend/interactive/jit_engine.hpp"
#include "frontend/ast/ast.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace nv {
namespace interactive {

/**
 * Interactive Session - Interface de alto nível para modo interativo
 * 
 * Esta classe integra todos os componentes do modo interativo:
 * - Session Manager
 * - Incremental Checker
 * - Epoch System (para notebooks)
 * - IR Incremental Builder
 * - JIT Execution Engine
 * 
 * Fornece APIs simples para:
 * - REPL (executar linha a linha)
 * - Notebook (executar células)
 */
class InteractiveSession {
public:
    /**
     * Modo de operação da sessão
     */
    enum class Mode {
        REPL,      // Modo REPL (linha a linha)
        NOTEBOOK   // Modo Notebook (células)
    };
    
    InteractiveSession(Mode mode = Mode::REPL);
    ~InteractiveSession() = default;
    
    /**
     * Executa código no modo REPL
     * 
     * @param source Código fonte a executar
     * @param source_name Nome da origem (ex: "repl", "line_1")
     * @return true se execução foi bem-sucedida
     */
    bool execute_repl(const std::string& source, const std::string& source_name = "repl");
    
    /**
     * Executa uma célula de notebook
     * 
     * @param cell_id ID da célula (ex: "cell_1")
     * @param source Código fonte da célula
     * @return true se execução foi bem-sucedida
     */
    bool execute_notebook_cell(const std::string& cell_id, const std::string& source);
    
    /**
     * Reexecuta uma célula de notebook (invalida dependências)
     * 
     * @param cell_id ID da célula a reexecutar
     * @param source Novo código fonte (ou mesmo código)
     * @return true se execução foi bem-sucedida
     */
    bool reexecute_notebook_cell(const std::string& cell_id, const std::string& source);
    
    /**
     * Obtém o resultado da última execução (se houver)
     */
    void* get_last_result() const { return last_result; }
    
    /**
     * Verifica se houve erros na última execução
     */
    bool has_errors() const { return last_has_errors; }
    
    /**
     * Obtém estatísticas da sessão
     */
    SessionManager::SessionStats get_stats() const {
        return session_manager.get_stats();
    }
    
    /**
     * Limpa toda a sessão (reset completo)
     */
    void clear();
    
    /**
     * Obtém o Session Manager (para acesso avançado)
     */
    SessionManager& get_session_manager() { return session_manager; }
    
    /**
     * Obtém o modo atual da sessão
     */
    Mode get_mode() const { return mode; }

private:
    Mode mode;
    
    // Componentes principais
    SessionManager session_manager;
    IncrementalChecker incremental_checker;
    std::unique_ptr<EpochSystem> epoch_system;  // Apenas para modo NOTEBOOK
    IncrementalIRBuilder ir_builder;
    JITEngine jit_engine;
    
    // Estado da última execução
    void* last_result;
    bool last_has_errors;
    
    // Rastreamento de unidades executadas (para limpeza)
    std::unordered_set<ExecutionUnitId> executed_units;
    
    /**
     * Processa código fonte: lexer -> parser -> AST
     */
    std::unique_ptr<Node> parse_source(const std::string& source, const std::string& source_name);
    
    /**
     * Executa uma unidade de execução completa
     */
    bool execute_unit(std::unique_ptr<ExecutionUnit> unit);
    
    /**
     * Processa uma unidade: análise -> IR -> execução
     */
    bool process_and_execute(ExecutionUnit& unit);
};

} // namespace interactive
} // namespace nv
