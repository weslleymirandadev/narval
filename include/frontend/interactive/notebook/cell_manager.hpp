#pragma once

#include <string>
#include <vector>
#include <memory>

namespace nv {

struct NotebookCell {
    int id;
    std::string source;
    std::string last_output;
};

class CellManager {
public:
    CellManager() = default;
    ~CellManager() = default;

    bool add_cell(const std::string& src);
    bool delete_cell(int id);
    bool run_cell(int id, class REPL* repl, const std::string& source_name = "");
    bool run_all(class REPL* repl);
    void list_cells();
    const std::vector<NotebookCell>& get_cells() const { return cells; }
    void clear_cells() { cells.clear(); next_id = 1; }

private:
    std::vector<NotebookCell> cells;
    int next_id = 1;
};

} // namespace nv
