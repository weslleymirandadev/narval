#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "frontend/interactive/interactive_session.hpp"

class Program;

namespace narval::frontend::interactive {

/**
 * ExecutionUnit
 *
 * Immutable representation of a single interactive fragment:
 * - One REPL step, or one notebook cell execution.
 *
 * It contains the original source, a parsed/checked AST (Program), and the
 * symbol interface of the fragment (defined/used).
 *
 * It must NOT contain execution/JIT/IR logic.
 */
class ExecutionUnit {
public:
    ExecutionUnit(
        std::string id,
        std::string virtual_filename,
        std::string source,
        std::optional<Origin> origin,
        int epoch,
        std::shared_ptr<const ::Program> ast,
        std::unordered_set<std::string> defined_symbols,
        std::unordered_set<std::string> used_symbols
    );

    ~ExecutionUnit();

    // Factory helpers.
    static ExecutionUnit create_repl_step(
        size_t step,
        std::string source,
        std::shared_ptr<const ::Program> ast,
        std::unordered_set<std::string> defined_symbols,
        std::unordered_set<std::string> used_symbols
    );

    static ExecutionUnit create_notebook_cell(
        std::string cell_id,
        int epoch,
        std::string source,
        std::shared_ptr<const ::Program> ast,
        std::unordered_set<std::string> defined_symbols,
        std::unordered_set<std::string> used_symbols
    );

    // --- Inspection APIs ---
    const std::string& id() const { return id_; }
    const std::string& virtual_filename() const { return virtual_filename_; }
    const std::string& source() const { return source_; }
    const std::optional<Origin>& origin() const { return origin_; }
    int epoch() const { return epoch_; }

    const ::Program& ast() const { return *ast_; }
    const std::shared_ptr<const ::Program>& ast_ptr() const { return ast_; }

    const std::unordered_set<std::string>& defined_symbols() const { return defined_symbols_; }
    const std::unordered_set<std::string>& used_symbols() const { return used_symbols_; }

    // Stable-ish hash for quick change detection.
    std::size_t content_hash() const { return content_hash_; }

    // Comparison helpers.
    bool same_content_as(const ExecutionUnit& other) const;

    // Convenience conversion to the simple wire-format used by SessionManager.
    IncrementalUnit to_incremental_unit() const;

private:
    std::string id_;
    std::string virtual_filename_;
    std::string source_;
    std::optional<Origin> origin_;
    int epoch_ = 0;

    std::shared_ptr<const ::Program> ast_;
    std::unordered_set<std::string> defined_symbols_;
    std::unordered_set<std::string> used_symbols_;

    std::size_t content_hash_ = 0;

    static std::size_t compute_hash(
        const std::string& source,
        const std::unordered_set<std::string>& defined,
        const std::unordered_set<std::string>& used
    );
};

} // namespace narval::frontend::interactive
