#pragma once

#include <string>
#include <vector>
#include <memory>
#include "frontend/interactive/repl.hpp"

namespace nv {

// Forward declarations
class CellManager;
class NotebookIO;
class NotebookUI;

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
    REPLConfig config;
    std::unique_ptr<REPL> repl;
    std::unique_ptr<CellManager> cell_manager;
    std::unique_ptr<NotebookIO> notebook_io;
    std::unique_ptr<NotebookUI> notebook_ui;

    std::string read_multiline();
};

} // namespace nv
