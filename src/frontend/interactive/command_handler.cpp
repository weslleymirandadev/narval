#include "frontend/interactive/command_handler.hpp"
#include "frontend/interactive/repl.hpp"
#include "frontend/interactive/repl_utils.hpp"
#include "frontend/interactive/syntax_highlighter.hpp"
#include <iostream>
#include <fstream>

namespace nv {

CommandHandler::CommandHandler(REPLState* state, const REPLConfig& config) 
    : state(state), config(config) {}

bool CommandHandler::execute_command(REPLCommand cmd, const std::vector<std::string>& args) {
    switch (cmd) {
        case REPLCommand::HELP:
            show_help();
            return true;
        case REPLCommand::QUIT:
        case REPLCommand::EXIT:
            return false;
        case REPLCommand::RESET:
            reset_state();
            return true;
        case REPLCommand::VARS:
            show_variables();
            return true;
        case REPLCommand::HISTORY:
            show_history();
            return true;
        case REPLCommand::LOAD:
            if (args.empty()) { 
                if (config.show_errors) std::cerr << "Usage: :load <filename>" << std::endl; 
                return true; 
            }
            return load_file(args[0]);
        case REPLCommand::SAVE:
            if (args.empty()) { 
                if (config.show_errors) std::cerr << "Usage: :save <filename>" << std::endl; 
                return true; 
            }
            return save_history(args[0]);
        default:
            return true;
    }
}

void CommandHandler::show_help() {
    std::cout << "\nNarval REPL Commands:\n";
    std::cout << "  :help, :h          Show this help message\n";
    std::cout << "  :quit, :exit, :q  Exit REPL\n";
    std::cout << "  :reset, :clear     Reset REPL context\n";
    std::cout << "  :vars, :v          Show defined variables\n";
    std::cout << "  :history, :hist    Show command history\n";
    std::cout << "  :load <file>       Load and execute file\n";
    std::cout << "  :save <file>       Save command history to file\n";
    std::cout << "\nFeatures:\n";
    std::cout << "  - Auto-print of expression results\n";
    std::cout << "  - Persistent variable state\n";
    std::cout << "  - Multiline input support\n";
    std::cout << "  - JIT compilation with LLVM\n";
    std::cout << "  - Type checking and error reporting\n";
    std::cout << std::endl;
}

void CommandHandler::show_variables() {
    std::cout << "\nDefined variables:\n";
    for (const auto& [name, addr] : state->symbols) {
        std::cout << "  " << name << " = " << repl_utils::format_value(addr) << std::endl;
    }
    std::cout << std::endl;
}

void CommandHandler::show_history() {
    std::cout << "\nCommand history:\n";
    for (size_t i = 0; i < state->history.size(); ++i) {
        std::cout << "  " << (i + 1) << ": "
                  << syntax_highlighter::highlight_source(state->history[i]) << std::endl;
    }
    std::cout << std::endl;
}

bool CommandHandler::load_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) { 
        if (config.show_errors) std::cerr << "Cannot open file: " << filename << std::endl; 
        return true; 
    }
    
    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;
        if (!line.empty()) {
            std::cout << "Loading line " << line_num << ": "
                      << syntax_highlighter::highlight_line(line) << std::endl;
            // Note: This would need access to compilation engine
            // For now, we'll just add to history
            state->history.push_back(line);
        }
    }
    return true;
}

bool CommandHandler::save_history(const std::string& filename) {
    std::ofstream file(filename);
    if (!file) { 
        if (config.show_errors) std::cerr << "Cannot create file: " << filename << std::endl; 
        return true; 
    }
    for (const auto& cmd : state->history) file << cmd << std::endl;
    std::cout << "History saved to " << filename << std::endl;
    return true;
}

void CommandHandler::reset_state() {
    state->reset();
    std::cout << "REPL state reset" << std::endl;
}

} // namespace nv
