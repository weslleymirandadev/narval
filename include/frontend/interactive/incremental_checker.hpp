#pragma once

#include "frontend/checker/checker.hpp"
#include "frontend/interactive/session_manager.hpp"
#include "frontend/interactive/execution_unit.hpp"
#include "frontend/ast/ast.hpp"
#include <memory>
#include <unordered_set>

namespace nv {
namespace interactive {

/**
 * Incremental Semantic Analyzer
 * 
 * Responsável por analisar apenas o necessário a cada nova execução.
 * 
 * Funções:
 * - Reanalisar apenas símbolos novos ou afetados
 * - Validar referências a símbolos existentes na sessão
 * - Detectar conflitos de redefinição
 * - Atualizar o grafo de dependências
 * - Produzir diagnósticos incrementais (erros e warnings locais)
 * 
 * Não reanalisa o programa inteiro.
 * Opera sempre em modo diferencial (diff-based).
 */
class IncrementalChecker {
public:
    IncrementalChecker(SessionManager& session_manager);
    ~IncrementalChecker() = default;
    
    /**
     * Analisa uma unidade de execução incrementalmente
     * 
     * @param unit Unidade a ser analisada
     * @return true se análise foi bem-sucedida (sem erros), false caso contrário
     */
    bool check_unit(ExecutionUnit& unit);
    
    /**
     * Reanalisa uma unidade após invalidação de dependências
     * 
     * @param unit Unidade a ser reanalisada
     * @return true se análise foi bem-sucedida
     */
    bool recheck_unit(ExecutionUnit& unit);
    
    /**
     * Obtém o checker interno (para acesso ao contexto de tipos, etc)
     */
    Checker& get_checker() { return checker; }
    const Checker& get_checker() const { return checker; }
    
    /**
     * Verifica se houve erros na última análise
     */
    bool has_errors() const { return checker.err; }

private:
    SessionManager& session_manager;
    Checker checker;
    
    /**
     * Coleta símbolos definidos em um nó AST
     */
    void collect_defined_symbols(Node* node, ExecutionUnit& unit);
    
    /**
     * Coleta símbolos usados em um nó AST
     */
    void collect_used_symbols(Node* node, ExecutionUnit& unit);
    
    /**
     * Verifica se todas as referências a símbolos são válidas
     */
    bool validate_symbol_references(ExecutionUnit& unit);
    
    /**
     * Atualiza dependências no SessionManager
     */
    void update_dependencies(ExecutionUnit& unit);
    
    /**
     * Processa um nó AST recursivamente para coletar símbolos
     */
    void process_node_for_symbols(Node* node, ExecutionUnit& unit);
};

} // namespace interactive
} // namespace nv
