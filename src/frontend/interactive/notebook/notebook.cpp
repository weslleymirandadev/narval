#include "frontend/interactive/notebook.hpp"
#include "frontend/interactive/notebook/cell_manager.hpp"
#include "frontend/interactive/notebook/notebook_io.hpp"
#include "frontend/interactive/notebook/notebook_ui.hpp"
#include <iostream>

namespace nv {

Notebook::Notebook(const REPLConfig& cfg) : config(cfg), repl(std::make_unique<REPL>(cfg)),
    cell_manager(std::make_unique<CellManager>()),
    notebook_io(std::make_unique<NotebookIO>()),
    notebook_ui(std::make_unique<NotebookUI>()) {}
Notebook::~Notebook() = default;

bool Notebook::initialize() {
    if (!repl) repl = std::make_unique<REPL>(REPLConfig{});
    if (!repl->initialize()) return false;
    std::cout << "Narval Notebook - simple interactive notebook" << std::endl;
    std::cout << "Type :help for commands, :exit to quit" << std::endl;
    return true;
}

std::string Notebook::read_multiline() {
    return notebook_ui->read_multiline();
}

bool Notebook::add_cell(const std::string& src) {
    return cell_manager->add_cell(src);
}

bool Notebook::run_cell(int id) {
    return cell_manager->run_cell(id, repl.get());
}

bool Notebook::run_all() {
    return cell_manager->run_all(repl.get());
}

bool Notebook::delete_cell(int id) {
    return cell_manager->delete_cell(id);
}

void Notebook::list_cells() {
    cell_manager->list_cells();
}

bool Notebook::save(const std::string& filename) {
    return notebook_io->save(filename, cell_manager->get_cells());
}

bool Notebook::load(const std::string& filename) {
    return notebook_io->load(filename, *cell_manager);
}

void Notebook::run() {
    while (true) {
        std::string line;
        notebook_ui->show_prompt();
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        if (line[0] == ':') {
            std::istringstream ss(line);
            std::string cmd; ss >> cmd;
            if (cmd == ":help") {
                notebook_ui->show_help();
            } else if (cmd == ":new") {
                std::string src = read_multiline();
                add_cell(src);
            } else if (cmd == ":list") {
                list_cells();
            } else if (cmd == ":runall") {
                run_all();
            } else if (cmd == ":run") {
                int id = 0; ss >> id; 
                if (id) run_cell(id); 
                else std::cerr << "Usage: :run <id>\n";
            } else if (cmd == ":del") {
                int id = 0; ss >> id; 
                if (id) delete_cell(id); 
                else std::cerr << "Usage: :del <id>\n";
            } else if (cmd == ":save") {
                std::string f; ss >> f; 
                if (!f.empty()) save(f); 
                else std::cerr << "Usage: :save <file>\n";
            } else if (cmd == ":load") {
                std::string f; ss >> f; 
                if (!f.empty()) load(f); 
                else std::cerr << "Usage: :load <file>\n";
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
            std::cout << "Added cell [" << cell_manager->get_cells().back().id << "] (use :run <id> to execute)\n";
        }
    }
}

} // namespace nv
