#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontend/interactive/interactive_session.hpp"

namespace narval::frontend::interactive {

class SessionManager;

/**
 * Epoch System (Notebook only)
 *
 * This component tracks logical versions (epochs) of a notebook session.
 * Each executed cell is associated with exactly one epoch.
 *
 * Re-executing a cell creates a new epoch for that cell and invalidates all
 * epochs that (transitively) depend on the old epoch.
 *
 * It does NOT execute code. It only manages epochs, dependencies and
 * invalidations, and integrates with SessionManager to invalidate symbols.
 */
struct Epoch {
    int id = 0;
    bool valid = true;

    // Cells that belong to this epoch.
    std::unordered_set<std::string> cells;

    // Semantic interface (aggregated from the executed cell fragment).
    std::unordered_set<std::string> defined_symbols;
    std::unordered_set<std::string> used_symbols;
};

class EpochManager {
public:
    EpochManager();

    void reset();

    // Creates a new epoch and assigns it to the given cell.
    // If the cell already had an epoch, the old epoch is returned via old_epoch.
    int create_epoch_for_cell(const std::string& cell_id, int* old_epoch = nullptr);

    // Commits semantic interface of the cell execution into the epoch.
    // This also updates epoch dependencies based on symbol producers.
    void commit_epoch(
        int epoch_id,
        const std::string& cell_id,
        const std::unordered_set<std::string>& defined_symbols,
        const std::unordered_set<std::string>& used_symbols
    );

    // Invalidates an epoch and all epochs that depend on it (transitively).
    // This invalidates the epoch itself as well.
    // If session is provided, invalidates symbols defined by affected epochs.
    // Returns the list of affected epoch ids (including epoch_id).
    std::vector<int> invalidate_epoch(int epoch_id, SessionManager* session);

    // Queries
    bool is_epoch_valid(int epoch_id) const;
    int get_epoch_of_cell(const std::string& cell_id) const;
    const Epoch* get_epoch(int epoch_id) const;

    std::vector<int> get_epoch_dependents(int epoch_id) const;
    std::vector<int> get_epoch_dependencies(int epoch_id) const;

    std::unordered_set<std::string> list_symbols_defined_by_epoch(int epoch_id) const;

    std::unordered_set<std::string> list_symbols_defined_by_epochs(const std::vector<int>& epoch_ids) const;

private:
    int epoch_counter_ = 0;

    // epoch_id -> epoch state
    std::unordered_map<int, Epoch> epochs_;

    // cell_id -> epoch_id
    std::unordered_map<std::string, int> cell_epoch_;

    // Epoch dependency graph: deps[A] = epochs that A depends on.
    std::unordered_map<int, std::unordered_set<int>> deps_;
    // Reverse: rdeps[B] = epochs that depend on B.
    std::unordered_map<int, std::unordered_set<int>> rdeps_;

    // Symbol -> producer epoch (last valid producer).
    std::unordered_map<std::string, int> symbol_producer_epoch_;

    void set_epoch_dependencies(int epoch_id, const std::unordered_set<int>& deps);
    std::vector<int> bfs_collect_dependents(int epoch_id);
    void invalidate_epoch_local(int epoch_id, SessionManager* session);
};

} // namespace narval::frontend::interactive
