#include "frontend/interactive/notebook/cell_manager.hpp"
#include "frontend/interactive/repl.hpp"
#include "frontend/interactive/syntax_highlighter.hpp"
#include <iostream>
#include <algorithm>

namespace nv {

bool CellManager::add_cell(const std::string& src) {
    NotebookCell c;
    c.id = next_id++;
    c.source = src;
    c.last_output.clear();
    cells.push_back(std::move(c));
    return true;
}

bool CellManager::delete_cell(int id) {
    auto it = std::find_if(cells.begin(), cells.end(), 
        [&](const NotebookCell& c){ return c.id == id; });
    if (it == cells.end()) return false;
    cells.erase(it);
    return true;
}

bool CellManager::run_cell(int id, REPL* repl, const std::string& source_name) {
    if (!repl) return false;
    
    for (auto &c : cells) {
        if (c.id == id) {
            bool ok = source_name.empty()
                ? repl->execute_source(c.source)
                : repl->execute_source(c.source, source_name);
            c.last_output = ok ? "(executed)" : "(error)";
            return ok;
        }
    }
    std::cerr << "Cell not found: " << id << std::endl;
    return false;
}

bool CellManager::run_all(REPL* repl) {
    if (!repl) return false;
    
    for (auto &c : cells) {
        if (!run_cell(c.id, repl)) return false;
    }
    return true;
}

void CellManager::list_cells() {
    std::cout << "Notebook cells:\n";
    for (const auto &c : cells) {
        std::string preview = c.source.size() > 60 ? c.source.substr(0, 60) + "..." : c.source;
        std::replace(preview.begin(), preview.end(), '\n', ' ');
        std::cout << "  [" << c.id << "] "
                  << syntax_highlighter::highlight_line(preview)
                  << "\n";
    }
}

} // namespace nv
