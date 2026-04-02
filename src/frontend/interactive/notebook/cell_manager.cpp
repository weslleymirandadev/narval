#include "frontend/interactive/notebook/cell_manager.hpp"
#include "frontend/interactive/repl.hpp"
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

bool CellManager::run_cell(int id, REPL* repl) {
    if (!repl) return false;
    
    for (auto &c : cells) {
        if (c.id == id) {
            bool ok = repl->execute_source(c.source);
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
        std::cout << "  [" << c.id << "] " 
                  << (c.source.size() > 60 ? c.source.substr(0,60) + "..." : c.source) 
                  << "\n";
    }
}

} // namespace nv
