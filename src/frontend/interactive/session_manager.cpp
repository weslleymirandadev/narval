#include "frontend/interactive/session_manager.hpp"
#include <algorithm>

namespace nv {
namespace interactive {

SessionManager::SessionManager()
    : next_unit_id(1) {
    // Criar namespace global inicial
    global_namespace = std::make_shared<Namespace>();
}

ExecutionUnitId SessionManager::create_unit_id() {
    return next_unit_id++;
}

bool SessionManager::define_symbol(const std::string& name,
                                   std::shared_ptr<Type> type,
                                   ExecutionUnitId unit_id,
                                   const std::string& source_name) {
    // Verificar se já existe
    if (session_symbols.find(name) != session_symbols.end()) {
        return false;  // Símbolo já existe (redefinição)
    }
    
    // Criar novo símbolo
    SymbolOrigin origin(unit_id, source_name);
    SessionSymbol symbol(type, origin);
    
    // Adicionar à tabela
    session_symbols[name] = symbol;
    
    // Registrar que esta unidade define este símbolo
    unit_symbols[unit_id].insert(name);
    
    // Adicionar ao namespace global para uso pelo checker
    global_namespace->put_key(name, type);
    
    return true;
}

bool SessionManager::update_symbol(const std::string& name,
                                  std::shared_ptr<Type> type,
                                  ExecutionUnitId unit_id,
                                  const std::string& source_name) {
    auto it = session_symbols.find(name);
    if (it == session_symbols.end()) {
        return false;  // Símbolo não existe
    }
    
    // Invalidar símbolo antigo e dependentes
    invalidate_symbol(name);
    
    // Atualizar símbolo
    SymbolOrigin origin(unit_id, source_name);
    it->second.type = type;
    it->second.origin = origin;
    it->second.is_valid = true;
    it->second.dependents.clear();
    
    // Registrar que esta unidade define este símbolo
    unit_symbols[unit_id].insert(name);
    
    // Atualizar namespace global
    global_namespace->put_key(name, type);
    
    return true;
}

SessionSymbol* SessionManager::lookup_symbol(const std::string& name) {
    auto it = session_symbols.find(name);
    if (it == session_symbols.end() || !it->second.is_valid) {
        return nullptr;
    }
    return &it->second;
}

bool SessionManager::has_symbol(const std::string& name) const {
    auto it = session_symbols.find(name);
    return it != session_symbols.end() && it->second.is_valid;
}

void SessionManager::add_dependency(ExecutionUnitId unit_id, const std::string& symbol_name) {
    // Registrar que esta unidade depende deste símbolo
    unit_dependencies[unit_id].insert(symbol_name);
    
    // Registrar que este símbolo tem esta unidade como dependente
    auto it = session_symbols.find(symbol_name);
    if (it != session_symbols.end()) {
        it->second.dependents.insert(unit_id);
    }
}

std::unordered_set<ExecutionUnitId> SessionManager::invalidate_symbol(const std::string& symbol_name) {
    std::unordered_set<ExecutionUnitId> invalidated;
    propagate_invalidation(symbol_name, invalidated);
    return invalidated;
}

std::unordered_set<ExecutionUnitId> SessionManager::invalidate_unit(ExecutionUnitId unit_id) {
    std::unordered_set<ExecutionUnitId> invalidated;
    
    // Obter todos os símbolos definidos por esta unidade
    auto symbols_it = unit_symbols.find(unit_id);
    if (symbols_it != unit_symbols.end()) {
        // Invalidar cada símbolo (faz cópia para evitar modificação durante iteração)
        std::vector<std::string> symbols_to_invalidate(
            symbols_it->second.begin(), 
            symbols_it->second.end()
        );
        
        for (const auto& symbol_name : symbols_to_invalidate) {
            auto unit_ids = invalidate_symbol(symbol_name);
            invalidated.insert(unit_ids.begin(), unit_ids.end());
        }
    }
    
    // Incluir a própria unidade
    invalidated.insert(unit_id);
    
    return invalidated;
}

std::vector<std::string> SessionManager::get_symbols_by_unit(ExecutionUnitId unit_id) const {
    std::vector<std::string> result;
    auto it = unit_symbols.find(unit_id);
    if (it != unit_symbols.end()) {
        result.assign(it->second.begin(), it->second.end());
    }
    return result;
}

std::unordered_set<std::string> SessionManager::get_unit_dependencies(ExecutionUnitId unit_id) const {
    std::unordered_set<std::string> result;
    auto it = unit_dependencies.find(unit_id);
    if (it != unit_dependencies.end()) {
        result = it->second;
    }
    return result;
}

void SessionManager::clear() {
    session_symbols.clear();
    unit_symbols.clear();
    unit_dependencies.clear();
    global_namespace = std::make_shared<Namespace>();
    next_unit_id = 1;
}

SessionManager::SessionStats SessionManager::get_stats() const {
    SessionStats stats;
    stats.total_symbols = session_symbols.size();
    stats.valid_symbols = 0;
    for (const auto& [name, symbol] : session_symbols) {
        if (symbol.is_valid) {
            stats.valid_symbols++;
        }
    }
    stats.total_units = unit_symbols.size();
    return stats;
}

void SessionManager::propagate_invalidation(const std::string& symbol_name,
                                            std::unordered_set<ExecutionUnitId>& invalidated) {
    auto it = session_symbols.find(symbol_name);
    if (it == session_symbols.end()) {
        return;  // Símbolo não existe
    }
    
    // Se já foi invalidado, não processar novamente
    if (!it->second.is_valid) {
        return;
    }
    
    // Invalidar símbolo
    it->second.is_valid = false;
    
    // Invalidar todas as unidades dependentes recursivamente
    for (ExecutionUnitId dependent_id : it->second.dependents) {
        if (invalidated.find(dependent_id) == invalidated.end()) {
            invalidated.insert(dependent_id);
            
            // Invalidar todos os símbolos definidos por esta unidade dependente
            auto symbols_it = unit_symbols.find(dependent_id);
            if (symbols_it != unit_symbols.end()) {
                for (const auto& dep_symbol_name : symbols_it->second) {
                    propagate_invalidation(dep_symbol_name, invalidated);
                }
            }
        }
    }
}

} // namespace interactive
} // namespace nv
