#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace narval::frontend::interactive {

enum class CellType {
    Code,
    Markdown,
};

struct Cell {
    std::string id;
    CellType type;
    std::string content;
    int epoch = 0;
    bool valid = true;
};

struct Origin {
    enum class Kind {
        ReplStep,
        NotebookCell,
    };

    Kind kind;
    std::string id;
};

struct IncrementalUnit {
    std::string id;
    std::string virtual_filename;
    std::string source;

    std::unordered_set<std::string> defined_symbols;
    std::unordered_set<std::string> used_symbols;

    std::optional<Origin> origin;
};

struct ExecutionResult {
    bool ok = false;
    std::string output;
    std::string error;

    std::unordered_set<std::string> defined_symbols;
    std::unordered_set<std::string> used_symbols;
};

class InteractiveOrchestrator;
class SessionManager;
class EpochManager;

class Repl;
class Notebook;

class InteractiveSession {
public:
    using OutputCallback = std::function<void(const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    virtual ~InteractiveSession() = default;

    void set_output_callback(OutputCallback cb) { out_cb_ = std::move(cb); }
    void set_error_callback(ErrorCallback cb) { err_cb_ = std::move(cb); }

    virtual void start() = 0;

    template <class T>
    T* get_session() {
        return dynamic_cast<T*>(this);
    }

protected:
    void emit_output(const std::string& s) {
        if (out_cb_) out_cb_(s);
    }

    void emit_error(const std::string& s) {
        if (err_cb_) err_cb_(s);
    }

private:
    OutputCallback out_cb_;
    ErrorCallback err_cb_;
};

class Repl : public InteractiveSession {
public:
    Repl();

    void start() override;

    ExecutionResult execute_line(const std::string& line);

    SessionManager& session_manager();

    void set_debug(bool enabled) { debug_ = enabled; }
    bool debug() const { return debug_; }

private:
    std::unique_ptr<InteractiveOrchestrator> orchestrator_;
    bool debug_ = false;
    size_t step_ = 0;
};

class Notebook : public InteractiveSession {
public:
    explicit Notebook(std::string title);

    void start() override;

    std::string create_cell(CellType type, const std::string& content);
    bool execute_cell(const std::string& cell_id);

    std::vector<std::string> get_cell_ids() const;
    const Cell* get_cell(const std::string& cell_id) const;

    void reset_session();

    bool save_to_file(const std::string& filename) const;

    SessionManager& session_manager();

    EpochManager& epoch_system();

private:
    std::string title_;
    std::unique_ptr<InteractiveOrchestrator> orchestrator_;
    std::unique_ptr<EpochManager> epochs_;

    int next_cell_id_ = 1;
    int current_epoch_ = 0;

    std::unordered_map<std::string, Cell> cells_;
};

std::unique_ptr<InteractiveSession> create_repl();
std::unique_ptr<InteractiveSession> create_notebook(const std::string& title);

} // namespace narval::frontend::interactive
