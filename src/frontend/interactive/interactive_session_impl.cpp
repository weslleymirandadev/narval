#include "frontend/interactive/interactive_session.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "frontend/interactive/epoch_system.hpp"
#include "frontend/interactive/interactive_orchestrator.hpp"

namespace narval::frontend::interactive {

Repl::Repl() : orchestrator_(std::make_unique<InteractiveOrchestrator>()) {}

void Repl::start() {
    // No-op for now: main.cpp drives the loop.
}

ExecutionResult Repl::execute_line(const std::string& line) {
    ++step_;

    IncrementalUnit u;
    u.id = std::to_string(step_);
    u.virtual_filename = "repl_line_" + u.id;
    u.source = line;
    u.origin = Origin{Origin::Kind::ReplStep, u.id};

    InteractiveOrchestrator::ExecuteOptions options;
    options.auto_print_last_expr = true;  // Ativar auto-print no REPL
    return orchestrator_->execute(u, options);
}

SessionManager& Repl::session_manager() {
    return orchestrator_->session_manager();
}

Notebook::Notebook(std::string title)
    : title_(std::move(title)),
      orchestrator_(std::make_unique<InteractiveOrchestrator>()),
      epochs_(std::make_unique<EpochManager>()) {}

void Notebook::start() {
    // No-op for now: main.cpp drives the loop.
}

std::string Notebook::create_cell(CellType type, const std::string& content) {
    const std::string id = "cell_" + std::to_string(next_cell_id_++);

    Cell c;
    c.id = id;
    c.type = type;
    c.content = content;
    c.epoch = 0;
    c.valid = true;

    cells_.emplace(id, std::move(c));
    return id;
}

bool Notebook::execute_cell(const std::string& cell_id) {
    auto it = cells_.find(cell_id);
    if (it == cells_.end()) return false;

    auto& cell = it->second;
    if (cell.type != CellType::Code) return true;

    // If the cell was executed before, this acts like a re-execution.
    int old_epoch = 0;
    const int new_epoch = epochs_->create_epoch_for_cell(cell_id, &old_epoch);
    cell.epoch = new_epoch;
    cell.valid = true;
    current_epoch_ = new_epoch;

    // If this is a re-execution, invalidate the old epoch and its dependents.
    if (old_epoch != 0) {
        auto affected_epochs = epochs_->invalidate_epoch(old_epoch, &session_manager());

        // Mark cells of affected epochs invalid.
        for (int e : affected_epochs) {
            const Epoch* ep = epochs_->get_epoch(e);
            if (!ep) continue;
            for (const auto& cid : ep->cells) {
                auto cit = cells_.find(cid);
                if (cit != cells_.end()) {
                    cit->second.valid = false;
                }
            }
        }
    }

    IncrementalUnit u;
    u.id = cell_id;
    u.virtual_filename = cell_id;
    u.source = cell.content;
    u.origin = Origin{Origin::Kind::NotebookCell, cell_id};

    auto result = orchestrator_->execute(u);
    if (!result.ok) {
        cell.valid = false;
        return false;
    }

    // Commit semantic interface into the epoch system.
    epochs_->commit_epoch(new_epoch, cell_id, result.defined_symbols, result.used_symbols);

    return true;
}

std::vector<std::string> Notebook::get_cell_ids() const {
    std::vector<std::string> out;
    out.reserve(cells_.size());
    for (const auto& [id, _] : cells_) out.push_back(id);

    std::sort(out.begin(), out.end());
    return out;
}

const Cell* Notebook::get_cell(const std::string& cell_id) const {
    auto it = cells_.find(cell_id);
    if (it == cells_.end()) return nullptr;
    return &it->second;
}

void Notebook::reset_session() {
    orchestrator_->reset();
    epochs_->reset();
    cells_.clear();

    next_cell_id_ = 1;
    current_epoch_ = 0;
}

bool Notebook::save_to_file(const std::string& filename) const {
    // Basic text format; content is not required for the epoch system.
    std::ostringstream oss;
    oss << "# " << title_ << "\n\n";

    auto ids = get_cell_ids();
    for (const auto& id : ids) {
        const auto* c = get_cell(id);
        if (!c) continue;

        oss << "## " << c->id << " (" << (c->type == CellType::Code ? "code" : "markdown") << ")";
        oss << " epoch=" << c->epoch << " valid=" << (c->valid ? "true" : "false") << "\n";
        oss << c->content << "\n\n";
    }

    std::ofstream out(filename);
    if (!out.is_open()) return false;
    out << oss.str();
    return true;
}

SessionManager& Notebook::session_manager() {
    return orchestrator_->session_manager();
}

EpochManager& Notebook::epoch_system() {
    return *epochs_;
}

std::unique_ptr<InteractiveSession> create_repl() {
    return std::make_unique<Repl>();
}

std::unique_ptr<InteractiveSession> create_notebook(const std::string& title) {
    return std::make_unique<Notebook>(title);
}

} // namespace narval::frontend::interactive
