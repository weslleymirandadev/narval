#include "frontend/interactive/notebook.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

#ifdef HAVE_NLOHMANN_JSON
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#endif

namespace nv {

Notebook::Notebook(const REPLConfig& cfg) : repl(std::make_unique<REPL>(cfg)) {}
Notebook::~Notebook() = default;

bool Notebook::initialize() {
    if (!repl) repl = std::make_unique<REPL>(REPLConfig{});
    if (!repl->initialize()) return false;
    std::cout << "Narval Notebook - simple interactive notebook" << std::endl;
    std::cout << "Type :help for commands, :exit to quit" << std::endl;
    return true;
}

std::string Notebook::read_multiline() {
    std::string buffer;
    std::string line;
    while (true) {
        std::cout << ">>> "; std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        if (line == ".") break;
        buffer += line + "\n";
    }
    return buffer;
}

bool Notebook::add_cell(const std::string& src) {
    NotebookCell c; c.id = next_id++; c.source = src; c.last_output.clear();
    cells.push_back(std::move(c));
    return true;
}

bool Notebook::run_cell(int id) {
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

bool Notebook::run_all() {
    for (auto &c : cells) {
        if (!run_cell(c.id)) return false;
    }
    return true;
}

bool Notebook::delete_cell(int id) {
    auto it = std::find_if(cells.begin(), cells.end(), [&](const NotebookCell& c){ return c.id == id; });
    if (it == cells.end()) return false;
    cells.erase(it);
    return true;
}

void Notebook::list_cells() {
    std::cout << "Notebook cells:\n";
    for (const auto &c : cells) {
        std::cout << "  [" << c.id << "] " << (c.source.size() > 60 ? c.source.substr(0,60) + "..." : c.source) << "\n";
    }
}

bool Notebook::save(const std::string& filename) {
#ifdef HAVE_NLOHMANN_JSON
    json j;
    j["cells"] = json::array();
    for (const auto &c : cells) {
        j["cells"].push_back({{"id", c.id}, {"source", c.source}, {"last_output", c.last_output}});
    }
    std::ofstream out(filename);
    if (!out) { std::cerr << "Failed to open file for write: " << filename << std::endl; return false; }
    out << j.dump(2);
    return true;
#else
    std::ofstream out(filename);
    if (!out) { std::cerr << "Failed to open file for write: " << filename << std::endl; return false; }
    for (const auto &c : cells) {
        out << "#cell:" << c.id << "\n";
        out << c.source << "\n";
        out << "#endcell\n";
    }
    return true;
#endif
}

bool Notebook::load(const std::string& filename) {
    std::ifstream in(filename);
    if (!in) { std::cerr << "Failed to open file for read: " << filename << std::endl; return false; }
#ifdef HAVE_NLOHMANN_JSON
    json j; in >> j;
    if (j.contains("cells") && j["cells"].is_array()) {
        cells.clear();
        for (auto &item : j["cells"]) {
            NotebookCell c; c.id = item.value("id", next_id++); c.source = item.value("source", std::string()); c.last_output = item.value("last_output", std::string());
            cells.push_back(std::move(c));
        }
    }
    return true;
#else
    cells.clear();
    std::string line; NotebookCell current; bool in_cell = false;
    while (std::getline(in, line)) {
        if (line.rfind("#cell:",0) == 0) {
            if (in_cell) { cells.push_back(current); }
            in_cell = true; current = NotebookCell(); current.id = std::stoi(line.substr(6)); current.source.clear();
            continue;
        }
        if (line == "#endcell") {
            if (in_cell) { cells.push_back(current); in_cell = false; }
            continue;
        }
        if (in_cell) {
            current.source += line + "\n";
        }
    }
    if (in_cell) cells.push_back(current);
    return true;
#endif
}

void Notebook::run() {
    while (true) {
        std::string line;
        std::cout << "nb> "; std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line[0] == ':') {
            std::istringstream ss(line);
            std::string cmd; ss >> cmd;
            if (cmd == ":help") {
                std::cout << ":help - show this\n";
                std::cout << ":new - create new cell (end with a single . on a line)\n";
                std::cout << ":list - list cells\n";
                std::cout << ":run <id> - run cell\n";
                std::cout << ":runall - run all cells\n";
                std::cout << ":del <id> - delete cell\n";
                std::cout << ":save <file> - save notebook\n";
                std::cout << ":load <file> - load notebook\n";
                std::cout << ":exit - exit notebook\n";
            } else if (cmd == ":new") {
                std::string src = read_multiline();
                add_cell(src);
            } else if (cmd == ":list") {
                list_cells();
            } else if (cmd == ":runall") {
                run_all();
            } else if (cmd == ":run") {
                int id = 0; ss >> id; if (id) run_cell(id); else std::cerr << "Usage: :run <id>\n";
            } else if (cmd == ":del") {
                int id = 0; ss >> id; if (id) delete_cell(id); else std::cerr << "Usage: :del <id>\n";
            } else if (cmd == ":save") {
                std::string f; ss >> f; if (!f.empty()) save(f); else std::cerr << "Usage: :save <file>\n";
            } else if (cmd == ":load") {
                std::string f; ss >> f; if (!f.empty()) load(f); else std::cerr << "Usage: :load <file>\n";
            } else if (cmd == ":exit" || cmd == ":quit") {
                break;
            } else {
                std::cerr << "Unknown command: " << cmd << std::endl;
            }
        } else {
            // Treat normal input as start of a multiline cell (terminated by a single '.')
            std::string src = line + "\n";
            while (true) {
                std::cout << ">>> "; std::cout.flush();
                if (!std::getline(std::cin, line)) break;
                if (line == ".") break;
                src += line + "\n";
            }
            add_cell(src);
            std::cout << "Added cell [" << cells.back().id << "] (use :run <id> to execute)\n";
        }
    }
}

} // namespace nv
