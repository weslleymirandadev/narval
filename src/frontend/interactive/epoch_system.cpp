#include "frontend/interactive/epoch_system.hpp"

#include <queue>

#include "frontend/interactive/session_manager.hpp"

namespace narval::frontend::interactive {

namespace {

static void erase_from_set_map(
    std::unordered_map<int, std::unordered_set<int>>& m,
    int key,
    int value
) {
    auto it = m.find(key);
    if (it == m.end()) return;
    it->second.erase(value);
    if (it->second.empty()) m.erase(it);
}

} // namespace

EpochManager::EpochManager() = default;

void EpochManager::reset() {
    epoch_counter_ = 0;
    epochs_.clear();
    cell_epoch_.clear();
    deps_.clear();
    rdeps_.clear();
    symbol_producer_epoch_.clear();
}

int EpochManager::create_epoch_for_cell(const std::string& cell_id, int* old_epoch) {
    int old = 0;
    if (auto it = cell_epoch_.find(cell_id); it != cell_epoch_.end()) {
        old = it->second;
    }
    if (old_epoch) *old_epoch = old;

    const int id = ++epoch_counter_;
    Epoch e;
    e.id = id;
    e.valid = true;
    e.cells.insert(cell_id);
    epochs_[id] = std::move(e);
    cell_epoch_[cell_id] = id;

    // New epoch starts with no deps; commit_epoch will fill.
    deps_[id].clear();
    return id;
}

void EpochManager::set_epoch_dependencies(int epoch_id, const std::unordered_set<int>& deps) {
    // Remove old reverse edges.
    if (auto it = deps_.find(epoch_id); it != deps_.end()) {
        for (const auto& old : it->second) {
            erase_from_set_map(rdeps_, old, epoch_id);
        }
    }

    deps_[epoch_id] = deps;
    for (const auto& dep : deps) {
        rdeps_[dep].insert(epoch_id);
    }
}

void EpochManager::commit_epoch(
    int epoch_id,
    const std::string& cell_id,
    const std::unordered_set<std::string>& defined_symbols,
    const std::unordered_set<std::string>& used_symbols
) {
    auto eit = epochs_.find(epoch_id);
    if (eit == epochs_.end()) return;

    auto& e = eit->second;
    e.valid = true;
    e.cells.insert(cell_id);
    e.defined_symbols = defined_symbols;
    e.used_symbols = used_symbols;

    // Epoch dependencies are derived from symbol producers.
    std::unordered_set<int> deps;
    for (const auto& used : used_symbols) {
        auto it = symbol_producer_epoch_.find(used);
        if (it != symbol_producer_epoch_.end()) {
            deps.insert(it->second);
        }
    }
    deps.erase(epoch_id);
    set_epoch_dependencies(epoch_id, deps);

    // Update producer epochs for defined symbols.
    for (const auto& def : defined_symbols) {
        symbol_producer_epoch_[def] = epoch_id;
    }
}

std::vector<int> EpochManager::bfs_collect_dependents(int epoch_id) {
    std::vector<int> out;
    std::queue<int> q;
    std::unordered_set<int> visited;

    for (const auto& dep : get_epoch_dependents(epoch_id)) {
        q.push(dep);
        visited.insert(dep);
    }

    while (!q.empty()) {
        const int cur = q.front();
        q.pop();
        out.push_back(cur);

        for (const auto& next : get_epoch_dependents(cur)) {
            if (visited.insert(next).second) {
                q.push(next);
            }
        }
    }

    return out;
}

void EpochManager::invalidate_epoch_local(int epoch_id, SessionManager* session) {
    auto it = epochs_.find(epoch_id);
    if (it == epochs_.end()) return;

    auto& e = it->second;
    if (!e.valid) return;
    e.valid = false;

    // Remove producers pointing to this invalidated epoch.
    for (auto pit = symbol_producer_epoch_.begin(); pit != symbol_producer_epoch_.end();) {
        if (pit->second == epoch_id) {
            pit = symbol_producer_epoch_.erase(pit);
        } else {
            ++pit;
        }
    }

    if (session) {
        for (const auto& def : e.defined_symbols) {
            session->invalidate_symbol(def);
        }
    }
}

std::vector<int> EpochManager::invalidate_epoch(int epoch_id, SessionManager* session) {
    std::vector<int> affected;

    // Invalidate the epoch itself first.
    invalidate_epoch_local(epoch_id, session);
    affected.push_back(epoch_id);

    // Invalidate dependents transitively.
    auto deps = bfs_collect_dependents(epoch_id);
    for (int e : deps) {
        invalidate_epoch_local(e, session);
        affected.push_back(e);
    }

    return affected;
}

bool EpochManager::is_epoch_valid(int epoch_id) const {
    auto it = epochs_.find(epoch_id);
    if (it == epochs_.end()) return false;
    return it->second.valid;
}

int EpochManager::get_epoch_of_cell(const std::string& cell_id) const {
    auto it = cell_epoch_.find(cell_id);
    if (it == cell_epoch_.end()) return 0;
    return it->second;
}

const Epoch* EpochManager::get_epoch(int epoch_id) const {
    auto it = epochs_.find(epoch_id);
    if (it == epochs_.end()) return nullptr;
    return &it->second;
}

std::vector<int> EpochManager::get_epoch_dependents(int epoch_id) const {
    auto it = rdeps_.find(epoch_id);
    if (it == rdeps_.end()) return {};
    return std::vector<int>(it->second.begin(), it->second.end());
}

std::vector<int> EpochManager::get_epoch_dependencies(int epoch_id) const {
    auto it = deps_.find(epoch_id);
    if (it == deps_.end()) return {};
    return std::vector<int>(it->second.begin(), it->second.end());
}

std::unordered_set<std::string> EpochManager::list_symbols_defined_by_epoch(int epoch_id) const {
    auto it = epochs_.find(epoch_id);
    if (it == epochs_.end()) return {};
    return it->second.defined_symbols;
}

std::unordered_set<std::string> EpochManager::list_symbols_defined_by_epochs(const std::vector<int>& epoch_ids) const {
    std::unordered_set<std::string> out;
    for (int id : epoch_ids) {
        auto it = epochs_.find(id);
        if (it == epochs_.end()) continue;
        out.insert(it->second.defined_symbols.begin(), it->second.defined_symbols.end());
    }
    return out;
}

} // namespace narval::frontend::interactive
