#pragma once

#include <string>
#include <vector>
#include "frontend/interactive/notebook/cell_manager.hpp"

namespace nv {

class NotebookIO {
public:
    NotebookIO() = default;
    ~NotebookIO() = default;

    bool save(const std::string& filename, const std::vector<NotebookCell>& cells);
    bool load(const std::string& filename, CellManager& cell_manager);

private:
    bool save_json(const std::string& filename, const std::vector<NotebookCell>& cells);
    bool save_text(const std::string& filename, const std::vector<NotebookCell>& cells);
    bool load_json(const std::string& filename, CellManager& cell_manager);
    bool load_text(const std::string& filename, CellManager& cell_manager);
};

} // namespace nv
