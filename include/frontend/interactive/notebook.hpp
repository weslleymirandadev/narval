#pragma once

#include <string>
#include <vector>
#include <memory>
#include "frontend/interactive/repl.hpp"

namespace nv {

struct NotebookCell {
    int id;
    std::string source;
    std::string last_output;
};

class Notebook {
public:
    explicit Notebook(const REPLConfig& cfg = REPLConfig{});
    ~Notebook();

    bool initialize();
    void run();

    bool add_cell(const std::string& src);
    bool run_cell(int id);
    bool run_all();
    bool delete_cell(int id);
    void list_cells();

    bool save(const std::string& filename);
    bool load(const std::string& filename);

private:
    std::unique_ptr<REPL> repl;
    std::vector<NotebookCell> cells;
    int next_id = 1;

    std::string read_multiline();
};

} // namespace nv
