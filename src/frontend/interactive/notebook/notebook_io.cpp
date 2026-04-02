#include "frontend/interactive/notebook/notebook_io.hpp"
#include "frontend/interactive/notebook/cell_manager.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

#ifdef HAVE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#endif

namespace nv {

bool NotebookIO::save(const std::string& filename, const std::vector<NotebookCell>& cells) {
#ifdef HAVE_NLOHMANN_JSON
    return save_json(filename, cells);
#else
    return save_text(filename, cells);
#endif
}

bool NotebookIO::load(const std::string& filename, CellManager& cell_manager) {
    std::ifstream in(filename);
    if (!in) { 
        std::cerr << "Failed to open file for read: " << filename << std::endl; 
        return false; 
    }

#ifdef HAVE_NLOHMANN_JSON
    return load_json(filename, cell_manager);
#else
    return load_text(filename, cell_manager);
#endif
}

bool NotebookIO::save_json(const std::string& filename, const std::vector<NotebookCell>& cells) {
#ifdef HAVE_NLOHMANN_JSON
    json j;
    j["cells"] = json::array();
    for (const auto &c : cells) {
        j["cells"].push_back({
            {"id", c.id}, 
            {"source", c.source}, 
            {"last_output", c.last_output}
        });
    }
    std::ofstream out(filename);
    if (!out) { 
        std::cerr << "Failed to open file for write: " << filename << std::endl; 
        return false; 
    }
    out << j.dump(2);
    return true;
#else
    return save_text(filename, cells);
#endif
}

bool NotebookIO::save_text(const std::string& filename, const std::vector<NotebookCell>& cells) {
    std::ofstream out(filename);
    if (!out) { 
        std::cerr << "Failed to open file for write: " << filename << std::endl; 
        return false; 
    }
    for (const auto &c : cells) {
        out << "#cell:" << c.id << "\n";
        out << c.source << "\n";
        out << "#endcell\n";
    }
    return true;
}

bool NotebookIO::load_json(const std::string& filename, CellManager& cell_manager) {
#ifdef HAVE_NLOHMANN_JSON
    std::ifstream in(filename);
    if (!in) { 
        std::cerr << "Failed to open file for read: " << filename << std::endl; 
        return false; 
    }
    
    json j; 
    in >> j;
    if (j.contains("cells") && j["cells"].is_array()) {
        cell_manager.clear_cells();
        for (auto &item : j["cells"]) {
            std::string source = item.value("source", std::string());
            cell_manager.add_cell(source);
        }
    }
    return true;
#else
    return load_text(filename, cell_manager);
#endif
}

bool NotebookIO::load_text(const std::string& filename, CellManager& cell_manager) {
    std::ifstream in(filename);
    if (!in) { 
        std::cerr << "Failed to open file for read: " << filename << std::endl; 
        return false; 
    }
    
    cell_manager.clear_cells();
    std::string line; 
    NotebookCell current; 
    bool in_cell = false;
    
    while (std::getline(in, line)) {
        if (line.rfind("#cell:",0) == 0) {
            if (in_cell) { 
                cell_manager.add_cell(current.source); 
            }
            in_cell = true; 
            current = NotebookCell(); 
            current.id = std::stoi(line.substr(6)); 
            current.source.clear();
            continue;
        }
        if (line == "#endcell") {
            if (in_cell) { 
                cell_manager.add_cell(current.source); 
                in_cell = false; 
            }
            continue;
        }
        if (in_cell) {
            current.source += line + "\n";
        }
    }
    if (in_cell) cell_manager.add_cell(current.source);
    return true;
}

} // namespace nv
