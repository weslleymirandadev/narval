#include "frontend/interactive/session_manager.hpp"

#include <queue>

namespace narval::frontend::interactive {

// ---------------- DependencyGraph ----------------

const std::unordered_set<std::string>& DependencyGraph::empty_set() {
    static const std::unordered_set<std::string> kEmpty;
    return kEmpty;
}

void DependencyGraph::clear() {
    deps_.clear();
    rdeps_.clear();
}

const std::unordered_set<std::string>& DependencyGraph::get_dependencies(const std::string& symbol) const {
    auto it = deps_.find(symbol);
    if (it == deps_.end()) return empty_set();
    return it->second;
}

const std::unordered_set<std::string>& DependencyGraph::get_dependents(const std::string& symbol) const {
    auto it = rdeps_.find(symbol);
    if (it == rdeps_.end()) return empty_set();
    return it->second;
}

void DependencyGraph::remove_symbol(const std::string& symbol) {
    // Remove reverse edges that point to `symbol`.
    if (auto it = deps_.find(symbol); it != deps_.end()) {
        for (const auto& dep : it->second) {
            auto rit = rdeps_.find(dep);
            if (rit != rdeps_.end()) {
                rit->second.erase(symbol);
                if (rit->second.empty()) rdeps_.erase(rit);
            }
        }
        deps_.erase(it);
    }

    // Remove forward edges from dependents of `symbol`.
    if (auto it = rdeps_.find(symbol); it != rdeps_.end()) {
        for (const auto& dependent : it->second) {
            auto dit = deps_.find(dependent);
            if (dit != deps_.end()) {
                dit->second.erase(symbol);
            }
        }
        rdeps_.erase(it);
    }
}

void DependencyGraph::set_dependencies(const std::string& symbol, const std::unordered_set<std::string>& deps) {
    // Remove old reverse edges for symbol.
    if (auto it = deps_.find(symbol); it != deps_.end()) {
        for (const auto& old_dep : it->second) {
            auto rit = rdeps_.find(old_dep);
            if (rit != rdeps_.end()) {
                rit->second.erase(symbol);
                if (rit->second.empty()) rdeps_.erase(rit);
            }
        }
    }

    deps_[symbol] = deps;

    // Add new reverse edges.
    for (const auto& dep : deps) {
        rdeps_[dep].insert(symbol);
    }
}

// ---------------- SessionSymbolTable ----------------

void SessionSymbolTable::clear() {
    symbols_.clear();
}

bool SessionSymbolTable::has(const std::string& name) const {
    return symbols_.find(name) != symbols_.end();
}

const SessionSymbol* SessionSymbolTable::get(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it == symbols_.end()) return nullptr;
    return &it->second;
}

SessionSymbol* SessionSymbolTable::get_mut(const std::string& name) {
    auto it = symbols_.find(name);
    if (it == symbols_.end()) return nullptr;
    return &it->second;
}

void SessionSymbolTable::put(SessionSymbol sym) {
    symbols_[sym.name] = std::move(sym);
}

std::vector<std::string> SessionSymbolTable::list_all() const {
    std::vector<std::string> out;
    out.reserve(symbols_.size());
    for (const auto& [k, _] : symbols_) out.push_back(k);
    return out;
}

std::vector<std::string> SessionSymbolTable::list_valid() const {
    std::vector<std::string> out;
    for (const auto& [k, v] : symbols_) {
        if (v.valid) out.push_back(k);
    }
    return out;
}

std::vector<std::string> SessionSymbolTable::list_invalid() const {
    std::vector<std::string> out;
    for (const auto& [k, v] : symbols_) {
        if (!v.valid) out.push_back(k);
    }
    return out;
}

// ---------------- SessionManager ----------------

SessionManager::SessionManager() = default;

void SessionManager::reset() {
    symtab_.clear();
    depgraph_.clear();
}

void SessionManager::add_symbol(
    const std::string& name,
    std::shared_ptr<nv::Type> type,
    const Origin& origin,
    const std::unordered_set<std::string>& dependencies
) {
    if (symtab_.has(name)) {
        redefine_symbol(name, std::move(type), origin, dependencies);
        return;
    }

    SessionSymbol sym;
    sym.name = name;
    sym.type = std::move(type);
    sym.origin = origin;
    sym.valid = true;
    sym.version = 1;
    symtab_.put(std::move(sym));

    depgraph_.set_dependencies(name, dependencies);
}

void SessionManager::redefine_symbol(
    const std::string& name,
    std::shared_ptr<nv::Type> type,
    const Origin& origin,
    const std::unordered_set<std::string>& dependencies
) {
    // Invalidate all dependents first (the symbol itself remains valid, but its
    // dependents become stale).
    invalidate_dependents_of(name);

    SessionSymbol sym;
    if (auto* existing = symtab_.get_mut(name)) {
        sym = *existing;
    } else {
        sym.name = name;
    }

    sym.type = std::move(type);
    sym.origin = origin;
    sym.valid = true;
    sym.version = sym.version + 1;

    symtab_.put(std::move(sym));
    depgraph_.set_dependencies(name, dependencies);
}

void SessionManager::validate_symbol(const std::string& name) {
    if (auto* s = symtab_.get_mut(name)) {
        s->valid = true;
    }
}

void SessionManager::invalidate_dependents_of(const std::string& name) {
    // BFS over reverse edges.
    std::queue<std::string> q;
    std::unordered_set<std::string> visited;

    for (const auto& dep : depgraph_.get_dependents(name)) {
        q.push(dep);
        visited.insert(dep);
    }

    while (!q.empty()) {
        auto cur = q.front();
        q.pop();

        if (auto* sym = symtab_.get_mut(cur)) {
            sym->valid = false;
        }

        for (const auto& dep : depgraph_.get_dependents(cur)) {
            if (visited.insert(dep).second) {
                q.push(dep);
            }
        }
    }
}

void SessionManager::invalidate_symbol(const std::string& name) {
    if (auto* sym = symtab_.get_mut(name)) {
        sym->valid = false;
    }
    invalidate_dependents_of(name);
}

void SessionManager::commit_unit(
    const IncrementalUnit& unit,
    const std::unordered_map<std::string, std::shared_ptr<nv::Type>>& defined_symbol_types
) {
    const Origin origin = unit.origin.value_or(Origin{Origin::Kind::ReplStep, unit.id});

    // For every defined symbol, add or redefine with dependencies = used_symbols.
    // This is a coarse but useful approximation: symbols defined in this unit
    // depend on symbols used in this unit.
    for (const auto& def : unit.defined_symbols) {
        std::shared_ptr<nv::Type> ty = nullptr;
        if (auto it = defined_symbol_types.find(def); it != defined_symbol_types.end()) {
            ty = it->second;
        }

        if (symtab_.has(def)) {
            redefine_symbol(def, ty, origin, unit.used_symbols);
        } else {
            add_symbol(def, ty, origin, unit.used_symbols);
        }
    }
}

std::vector<std::string> SessionManager::list_symbols_all() const {
    return symtab_.list_all();
}

std::vector<std::string> SessionManager::list_symbols_valid() const {
    return symtab_.list_valid();
}

std::vector<std::string> SessionManager::list_symbols_invalid() const {
    return symtab_.list_invalid();
}

bool SessionManager::is_symbol_valid(const std::string& name) const {
    auto* sym = symtab_.get(name);
    return sym && sym->valid;
}

const Origin* SessionManager::get_origin(const std::string& name) const {
    auto* sym = symtab_.get(name);
    if (!sym) return nullptr;
    return &sym->origin;
}

std::shared_ptr<nv::Type> SessionManager::get_type(const std::string& name) const {
    auto* sym = symtab_.get(name);
    if (!sym) return nullptr;
    return sym->type;
}

std::unordered_set<std::string> SessionManager::get_dependencies(const std::string& name) const {
    return depgraph_.get_dependencies(name);
}

std::unordered_set<std::string> SessionManager::get_dependents(const std::string& name) const {
    return depgraph_.get_dependents(name);
}

} // namespace narval::frontend::interactive
