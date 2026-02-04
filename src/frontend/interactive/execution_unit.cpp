#include "frontend/interactive/execution_unit.hpp"

#include <functional>

#include "frontend/ast/program.hpp"

namespace narval::frontend::interactive {

namespace {

static inline void hash_combine(std::size_t& seed, std::size_t v) {
    // A common hash combine pattern.
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

static std::size_t hash_set(const std::unordered_set<std::string>& s) {
    // Order-independent hash.
    std::size_t seed = 0;
    for (const auto& item : s) {
        hash_combine(seed, std::hash<std::string>{}(item));
    }
    return seed;
}

} // namespace

ExecutionUnit::ExecutionUnit(
    std::string id,
    std::string virtual_filename,
    std::string source,
    std::optional<Origin> origin,
    int epoch,
    std::shared_ptr<const ::Program> ast,
    std::unordered_set<std::string> defined_symbols,
    std::unordered_set<std::string> used_symbols
)
    : id_(std::move(id)),
      virtual_filename_(std::move(virtual_filename)),
      source_(std::move(source)),
      origin_(std::move(origin)),
      epoch_(epoch),
      ast_(std::move(ast)),
      defined_symbols_(std::move(defined_symbols)),
      used_symbols_(std::move(used_symbols)),
      content_hash_(compute_hash(source_, defined_symbols_, used_symbols_)) {}

ExecutionUnit::~ExecutionUnit() = default;

ExecutionUnit ExecutionUnit::create_repl_step(
    size_t step,
    std::string source,
    std::shared_ptr<const ::Program> ast,
    std::unordered_set<std::string> defined_symbols,
    std::unordered_set<std::string> used_symbols
) {
    const std::string id = std::to_string(step);
    const std::string vfile = "repl_line_" + id;
    std::optional<Origin> origin = Origin{Origin::Kind::ReplStep, id};
    return ExecutionUnit(id, vfile, std::move(source), origin, 0, std::move(ast), std::move(defined_symbols), std::move(used_symbols));
}

ExecutionUnit ExecutionUnit::create_notebook_cell(
    std::string cell_id,
    int epoch,
    std::string source,
    std::shared_ptr<const ::Program> ast,
    std::unordered_set<std::string> defined_symbols,
    std::unordered_set<std::string> used_symbols
) {
    const std::string vfile = cell_id;
    std::optional<Origin> origin = Origin{Origin::Kind::NotebookCell, cell_id};
    return ExecutionUnit(cell_id, vfile, std::move(source), origin, epoch, std::move(ast), std::move(defined_symbols), std::move(used_symbols));
}

bool ExecutionUnit::same_content_as(const ExecutionUnit& other) const {
    return content_hash_ == other.content_hash_ &&
           source_ == other.source_ &&
           defined_symbols_ == other.defined_symbols_ &&
           used_symbols_ == other.used_symbols_;
}

IncrementalUnit ExecutionUnit::to_incremental_unit() const {
    IncrementalUnit u;
    u.id = id_;
    u.virtual_filename = virtual_filename_;
    u.source = source_;
    u.defined_symbols = defined_symbols_;
    u.used_symbols = used_symbols_;
    u.origin = origin_;
    return u;
}

std::size_t ExecutionUnit::compute_hash(
    const std::string& source,
    const std::unordered_set<std::string>& defined,
    const std::unordered_set<std::string>& used
) {
    std::size_t seed = 0;
    hash_combine(seed, std::hash<std::string>{}(source));
    hash_combine(seed, hash_set(defined));
    hash_combine(seed, hash_set(used));
    return seed;
}

} // namespace narval::frontend::interactive
