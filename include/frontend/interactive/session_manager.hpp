#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontend/interactive/interactive_session.hpp"

#include "frontend/checker/type.hpp"

namespace narval::frontend::interactive {

/**
 * SessionManager
 *
 * Central semantic state for interactive mode.
 *
 * Design constraints:
 * - No LLVM/IR/JIT knowledge.
 * - Operates only on semantic symbols, types, and dependency edges.
 * - Supports invalidation and redefinition with fast propagation.
 */

struct SessionSymbol {
    std::string name;
    std::shared_ptr<nv::Type> type;
    Origin origin{Origin::Kind::ReplStep, ""};
    bool valid = true;

    // Monotonic version incremented on redefinition.
    // Useful for higher-level systems (e.g. epochs) to reason about staleness.
    uint64_t version = 0;
};

/**
 * Stores per-symbol dependency info.
 *
 * deps[A] = symbols that A depends on.
 * rdeps[B] = symbols that depend on B.
 *
 * Keeping reverse edges allows invalidation to be O(affected) rather than
 * O(all symbols).
 */
class DependencyGraph {
public:
    void clear();

    void set_dependencies(const std::string& symbol, const std::unordered_set<std::string>& deps);
    void remove_symbol(const std::string& symbol);

    const std::unordered_set<std::string>& get_dependencies(const std::string& symbol) const;
    const std::unordered_set<std::string>& get_dependents(const std::string& symbol) const;

private:
    std::unordered_map<std::string, std::unordered_set<std::string>> deps_;
    std::unordered_map<std::string, std::unordered_set<std::string>> rdeps_;

    static const std::unordered_set<std::string>& empty_set();
};

/**
 * Symbol table for interactive session.
 *
 * NOTE: This is not the same as the checker's scope chain.
 * The checker is used to *infer types*; SessionSymbolTable tracks the
 * interactive session state and validity.
 */
class SessionSymbolTable {
public:
    void clear();

    bool has(const std::string& name) const;
    const SessionSymbol* get(const std::string& name) const;
    SessionSymbol* get_mut(const std::string& name);

    // Insert or overwrite.
    void put(SessionSymbol sym);

    std::vector<std::string> list_all() const;
    std::vector<std::string> list_valid() const;
    std::vector<std::string> list_invalid() const;

private:
    std::unordered_map<std::string, SessionSymbol> symbols_;
};

class SessionManager {
public:
    SessionManager();

    void reset();

    // --- Symbol lifecycle ---

    // Adds a new symbol. If it already exists, behaves like redefine_symbol().
    void add_symbol(
        const std::string& name,
        std::shared_ptr<nv::Type> type,
        const Origin& origin,
        const std::unordered_set<std::string>& dependencies
    );

    // Redefines symbol and invalidates its dependents.
    void redefine_symbol(
        const std::string& name,
        std::shared_ptr<nv::Type> type,
        const Origin& origin,
        const std::unordered_set<std::string>& dependencies
    );

    // Invalidates a symbol and recursively invalidates all its dependents.
    void invalidate_symbol(const std::string& name);

    // Marks a symbol valid again without touching dependents (rarely used;
    // most callers should re-execute/redefine instead).
    void validate_symbol(const std::string& name);

    // Convenience: commit a full incremental unit.
    // This is where other components hand over a unit's definitions and
    // symbol usage. Types are optional and may be missing.
    void commit_unit(
        const IncrementalUnit& unit,
        const std::unordered_map<std::string, std::shared_ptr<nv::Type>>& defined_symbol_types = {}
    );

    // --- Queries ---
    const SessionSymbolTable& symbols() const { return symtab_; }
    const DependencyGraph& deps() const { return depgraph_; }

    std::vector<std::string> list_symbols_all() const;
    std::vector<std::string> list_symbols_valid() const;
    std::vector<std::string> list_symbols_invalid() const;

    bool is_symbol_valid(const std::string& name) const;
    const Origin* get_origin(const std::string& name) const;
    std::shared_ptr<nv::Type> get_type(const std::string& name) const;

    std::unordered_set<std::string> get_dependencies(const std::string& name) const;
    std::unordered_set<std::string> get_dependents(const std::string& name) const;

private:
    SessionSymbolTable symtab_;
    DependencyGraph depgraph_;

    void invalidate_dependents_of(const std::string& name);
};

} // namespace narval::frontend::interactive
