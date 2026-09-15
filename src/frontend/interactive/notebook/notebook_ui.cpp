#include "frontend/interactive/notebook/notebook_ui.hpp"
#include "frontend/interactive/line_editor.hpp"
#include "frontend/version.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace nv {
namespace {

std::string continuation_prompt(int execution_count) {
    std::ostringstream out;
    out << "   ..." << std::setw(static_cast<int>(std::to_string(execution_count).size()) + 2) << ": ";
    return out.str();
}

// The version belongs in the prompt the same way it does in the REPL: it says which compiler the
// session is running (include/frontend/version.hpp).
std::string input_prompt(int execution_count) {
    return version_prefix() + " In [" + std::to_string(execution_count) + "]: ";
}

} // namespace

const std::string NotebookUI::PROMPT = version_prefix() + " nb> ";
const std::string NotebookUI::MULTILINE_PROMPT = " ";

std::string NotebookUI::read_multiline() {
    std::string buffer;
    std::string line;
    while (true) {
        line = line_editor::read_line(MULTILINE_PROMPT, false);
        if (line.empty() && std::cin.eof()) break;
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
    std::cout << input_prompt(execution_count);
    std::cout.flush();
}

void NotebookUI::show_continuation_prompt(int execution_count) {
    std::cout << continuation_prompt(execution_count);
    std::cout.flush();
}

std::string NotebookUI::read_input_line(int execution_count) {
    return line_editor::read_line(input_prompt(execution_count), false);
}

std::string NotebookUI::read_continuation_line(int execution_count) {
    return line_editor::read_line(continuation_prompt(execution_count), false);
}

} // namespace nv
