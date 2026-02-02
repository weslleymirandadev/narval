#pragma once

#include "frontend/interactive/session_manager.hpp"
#include "frontend/interactive/execution_unit.hpp"
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

namespace nv {
namespace interactive {

/**
 * Epoch System - Sistema de versões lógicas para Notebooks
 * 
 * Usado apenas no contexto de Notebooks.
 * 
 * Definições:
 * - Cada célula pertence a um "epoch"
 * - Um epoch representa uma versão lógica da sessão
 * 
 * Regras:
 * - Executar uma célula cria ou atualiza um epoch
 * - Reexecutar uma célula invalida todos os epochs que dependem dela
 * - Símbolos definidos em epochs invalidados tornam-se indisponíveis
 * - O sistema deve impedir uso de símbolos inválidos
 */
class EpochSystem {
public:
    using EpochId = uint64_t;
    using CellId = std::string;  // ID da célula (ex: "cell_1", "cell_2")
    
    EpochSystem(SessionManager& session_manager);
    ~EpochSystem() = default;
    
    /**
     * Executa uma célula, criando ou atualizando seu epoch
     * 
     * @param cell_id ID da célula
     * @param unit_id ID da unidade de execução associada
     * @return ID do epoch criado/atualizado
     */
    EpochId execute_cell(const CellId& cell_id, ExecutionUnitId unit_id);
    
    /**
     * Reexecuta uma célula, invalidando dependências
     * 
     * @param cell_id ID da célula a reexecutar
     * @param unit_id ID da nova unidade de execução
     * @return ID do novo epoch e set de epochs invalidados
     */
    struct ReexecutionResult {
        EpochId new_epoch_id;
        std::unordered_set<EpochId> invalidated_epochs;
        std::unordered_set<ExecutionUnitId> invalidated_units;
    };
    ReexecutionResult reexecute_cell(const CellId& cell_id, ExecutionUnitId unit_id);
    
    /**
     * Verifica se um epoch está válido
     */
    bool is_epoch_valid(EpochId epoch_id) const;
    
    /**
     * Obtém o epoch atual de uma célula
     */
    EpochId get_cell_epoch(const CellId& cell_id) const;
    
    /**
     * Obtém todas as células que dependem de uma célula específica
     */
    std::unordered_set<CellId> get_dependent_cells(const CellId& cell_id) const;
    
    /**
     * Limpa todo o sistema de epochs
     */
    void clear();

private:
    SessionManager& session_manager;
    
    // Mapeamento: cell_id -> epoch_id atual
    std::unordered_map<CellId, EpochId> cell_epochs;
    
    // Mapeamento: epoch_id -> cell_id
    std::unordered_map<EpochId, CellId> epoch_cells;
    
    // Mapeamento: epoch_id -> unit_id
    std::unordered_map<EpochId, ExecutionUnitId> epoch_units;
    
    // Grafo de dependências: cell_id -> células que dependem dela
    std::unordered_map<CellId, std::unordered_set<CellId>> cell_dependencies;
    
    // Set de epochs válidos
    std::unordered_set<EpochId> valid_epochs;
    
    // Contador para gerar IDs de epoch únicos
    EpochId next_epoch_id;
    
    /**
     * Propaga invalidação através das dependências de células
     */
    void propagate_cell_invalidation(const CellId& cell_id,
                                     std::unordered_set<EpochId>& invalidated);
    
    /**
     * Atualiza dependências entre células baseado nas dependências de símbolos
     */
    void update_cell_dependencies(const CellId& cell_id, ExecutionUnitId unit_id);
};

} // namespace interactive
} // namespace nv
