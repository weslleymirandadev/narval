#include "frontend/interactive/epoch_system.hpp"

namespace nv {
namespace interactive {

EpochSystem::EpochSystem(SessionManager& session_manager)
    : session_manager(session_manager), next_epoch_id(1) {
}

EpochSystem::EpochId EpochSystem::execute_cell(const CellId& cell_id, ExecutionUnitId unit_id) {
    // Verificar se a célula já tem um epoch
    auto it = cell_epochs.find(cell_id);
    
    if (it != cell_epochs.end()) {
        // Célula já executada - reexecutar
        return reexecute_cell(cell_id, unit_id).new_epoch_id;
    }
    
    // Criar novo epoch
    EpochId epoch_id = next_epoch_id++;
    
    cell_epochs[cell_id] = epoch_id;
    epoch_cells[epoch_id] = cell_id;
    epoch_units[epoch_id] = unit_id;
    valid_epochs.insert(epoch_id);
    
    // Atualizar dependências entre células
    update_cell_dependencies(cell_id, unit_id);
    
    return epoch_id;
}

EpochSystem::ReexecutionResult EpochSystem::reexecute_cell(const CellId& cell_id, ExecutionUnitId unit_id) {
    ReexecutionResult result;
    
    // Obter epoch anterior
    auto old_epoch_it = cell_epochs.find(cell_id);
    EpochId old_epoch_id = 0;
    ExecutionUnitId old_unit_id = 0;
    
    if (old_epoch_it != cell_epochs.end()) {
        old_epoch_id = old_epoch_it->second;
        auto unit_it = epoch_units.find(old_epoch_id);
        if (unit_it != epoch_units.end()) {
            old_unit_id = unit_it->second;
        }
    }
    
    // Invalidar dependências da célula antiga
    std::unordered_set<EpochId> invalidated_epochs;
    propagate_cell_invalidation(cell_id, invalidated_epochs);
    
    // Invalidar unidades associadas aos epochs invalidados
    for (EpochId epoch_id : invalidated_epochs) {
        auto unit_it = epoch_units.find(epoch_id);
        if (unit_it != epoch_units.end()) {
            result.invalidated_units.insert(unit_it->second);
        }
    }
    
    // Invalidar a própria célula antiga se existir
    if (old_epoch_id != 0) {
        invalidated_epochs.insert(old_epoch_id);
        valid_epochs.erase(old_epoch_id);
        if (old_unit_id != 0) {
            result.invalidated_units.insert(old_unit_id);
        }
    }
    
    // Criar novo epoch
    EpochId new_epoch_id = next_epoch_id++;
    cell_epochs[cell_id] = new_epoch_id;
    epoch_cells[new_epoch_id] = cell_id;
    epoch_units[new_epoch_id] = unit_id;
    valid_epochs.insert(new_epoch_id);
    
    // Invalidar símbolos da unidade antiga
    if (old_unit_id != 0) {
        auto invalidated = session_manager.invalidate_unit(old_unit_id);
        result.invalidated_units.insert(invalidated.begin(), invalidated.end());
    }
    
    // Atualizar dependências
    update_cell_dependencies(cell_id, unit_id);
    
    result.new_epoch_id = new_epoch_id;
    result.invalidated_epochs = invalidated_epochs;
    
    return result;
}

bool EpochSystem::is_epoch_valid(EpochId epoch_id) const {
    return valid_epochs.find(epoch_id) != valid_epochs.end();
}

EpochSystem::EpochId EpochSystem::get_cell_epoch(const CellId& cell_id) const {
    auto it = cell_epochs.find(cell_id);
    if (it != cell_epochs.end()) {
        return it->second;
    }
    return 0;  // Célula nunca foi executada
}

std::unordered_set<EpochSystem::CellId> EpochSystem::get_dependent_cells(const CellId& cell_id) const {
    std::unordered_set<CellId> result;
    auto it = cell_dependencies.find(cell_id);
    if (it != cell_dependencies.end()) {
        result = it->second;
    }
    return result;
}

void EpochSystem::clear() {
    cell_epochs.clear();
    epoch_cells.clear();
    epoch_units.clear();
    cell_dependencies.clear();
    valid_epochs.clear();
    next_epoch_id = 1;
}

void EpochSystem::propagate_cell_invalidation(const CellId& cell_id,
                                               std::unordered_set<EpochId>& invalidated) {
    // Encontrar todas as células que dependem desta
    auto deps_it = cell_dependencies.find(cell_id);
    if (deps_it == cell_dependencies.end()) {
        return;
    }
    
    // Invalidar cada célula dependente recursivamente
    for (const CellId& dependent_cell : deps_it->second) {
        auto epoch_it = cell_epochs.find(dependent_cell);
        if (epoch_it != cell_epochs.end()) {
            EpochId epoch_id = epoch_it->second;
            if (invalidated.find(epoch_id) == invalidated.end()) {
                invalidated.insert(epoch_id);
                valid_epochs.erase(epoch_id);
                
                // Propagação recursiva
                propagate_cell_invalidation(dependent_cell, invalidated);
            }
        }
    }
}

void EpochSystem::update_cell_dependencies(const CellId& cell_id, ExecutionUnitId unit_id) {
    // Obter símbolos usados por esta unidade
    auto used_symbols = session_manager.get_unit_dependencies(unit_id);
    
    // Para cada símbolo usado, encontrar qual célula o definiu
    for (const auto& symbol_name : used_symbols) {
        auto* symbol = session_manager.lookup_symbol(symbol_name);
        if (symbol) {
            // Encontrar a célula que definiu este símbolo
            // Assumimos que o source_name da origem é o cell_id
            const CellId& defining_cell = symbol->origin.source_name;
            
            if (defining_cell != cell_id) {
                // Registrar dependência
                cell_dependencies[defining_cell].insert(cell_id);
            }
        }
    }
}

} // namespace interactive
} // namespace nv
