#include "frontend/interactive/notebook/notebook_ui.hpp"
#include <iostream>
#include <iomanip>

namespace nv {

const std::string NotebookUI::PROMPT = "nb> ";
const std::string NotebookUI::MULTILINE_PROMPT = " ";

std::string NotebookUI::read_multiline() {
    std::string buffer;
    std::string line;
    while (true) {
        std::cout << MULTILINE_PROMPT; 
        std::cout.flush();
        if (!std::getline(std::cin, line)) break;
        if (line == ".") break;
        buffer += line + "\n";
    }
    return buffer;
}

void NotebookUI::show_help() {
    std::cout << ":help - show this\n";
    std::cout << ":new - create new cell (end with a single . on a line)\n";
    std::cout << ":list - list cells\n";
    std::cout << ":run <id> - run cell\n";
    std::cout << ":runall - run all cells\n";
    std::cout << ":del <id> - delete cell\n";
    std::cout << ":save <file> - save notebook\n";
    std::cout << ":load <file> - load notebook\n";
    std::cout << ":exit - exit notebook\n";
}

void NotebookUI::show_prompt() {
    std::cout << PROMPT;
    std::cout.flush();
}

void NotebookUI::show_input_prompt(int execution_count) {
    std::cout << "In [" << execution_count << "]: ";
    std::cout.flush();
}

void NotebookUI::show_continuation_prompt(int execution_count) {
    std::cout << "   ..." << std::setw(static_cast<int>(std::to_string(execution_count).size()) + 2) << ": ";
    std::cout.flush();
}

} // namespace nv
