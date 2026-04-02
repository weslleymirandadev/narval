#include "frontend/interactive/repl.hpp"
#include "frontend/interactive/input_processor.hpp"
#include "frontend/interactive/compilation_engine.hpp"
#include "frontend/interactive/command_handler.hpp"
#include "frontend/module_manager.hpp"

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#ifdef RETURN
#undef RETURN
#endif
#endif

namespace nv {

REPL::REPL(const REPLConfig& config) : config(config), state(std::make_unique<REPLState>()),
    input_processor(std::make_unique<InputProcessor>(config)),
    compilation_engine(std::make_unique<CompilationEngine>(state.get(), module_manager)),
    command_handler(std::make_unique<CommandHandler>(state.get(), config)) {
    
    // Set config reference in state for other modules
    state->config = &this->config;
    
#ifdef HAVE_READLINE
    if (config.enable_readline) {
        rl_bind_key('\t', rl_complete);
    }
#endif
}

REPL::~REPL() = default;

bool REPL::initialize() {
    if (!state->initialize()) {
        std::cerr << "Failed to initialize REPL" << std::endl;
        return false;
    }
    std::cout << "Narval REPL - Interactive Compiler" << std::endl;
    std::cout << "Type :help for available commands or :quit to exit" << std::endl;
    return true;
}

std::string REPL::read_input() {
    return input_processor->read_input(in_multiline);
}

bool REPL::is_complete_expression(const std::string& input) {
    return input_processor->is_complete_expression(input);
}

std::string REPL::preprocess_input(const std::string& input) {
    return input_processor->preprocess_input(input);
}

bool REPL::execute_source(const std::string& src) {
    std::string processed = preprocess_input(src);
    return compilation_engine->compile_and_execute(processed);
}

REPLState* REPL::get_state() {
    return state.get();
}

bool REPL::should_auto_print(const std::string& input) {
    return input_processor->should_auto_print(input);
}

std::string REPL::generate_result_var() {
    return state->last_result_var + std::to_string(state->result_counter++);
}

void REPL::print_value(llvm::JITTargetAddress addr) {
    compilation_engine->print_value(addr);
}

bool REPL::execute_command(REPLCommand cmd, const std::vector<std::string>& args) {
    return command_handler->execute_command(cmd, args);
}

void REPL::show_help() {
    command_handler->show_help();
}

void REPL::show_variables() {
    command_handler->show_variables();
}

void REPL::show_history() {
    command_handler->show_history();
}

bool REPL::load_file(const std::string& filename) {
    return command_handler->load_file(filename);
}

bool REPL::save_history(const std::string& filename) {
    return command_handler->save_history(filename);
}

void REPL::reset_state() {
    command_handler->reset_state();
}

void REPL::handle_error(const std::string& error) {
    if (config.show_errors) std::cerr << "Error: " << error << std::endl;
}

void REPL::handle_warning(const std::string& warning) {
    if (config.show_warnings) std::cout << "Warning: " << warning << std::endl;
}

void REPL::add_to_history(const std::string& command) {
    state->history.push_back(command);
    if (state->history.size() > static_cast<size_t>(config.max_history)) 
        state->history.erase(state->history.begin());
}

void REPL::run() {
    while (true) {
        try {
            std::string line = read_input();
            if (line.empty() && std::cin.eof()) { 
                std::cout << std::endl; 
                break; 
            }
            if (line.empty()) continue;
            if (line[0] == ':') {
                auto [cmd, args] = CommandParser::parse(line);
                if (cmd != REPLCommand::NONE) {
                    if (!execute_command(cmd, args)) break;
                    continue;
                }
            }
            if (in_multiline) current_input += "\n" + line; 
            else current_input = line;
            if (!is_complete_expression(current_input)) { 
                in_multiline = true; 
                continue; 
            }
            in_multiline = false;
            add_to_history(current_input);
            std::string processed = preprocess_input(current_input);
            execute_source(processed);
            current_input.clear();
        } catch (const std::exception& e) {
            handle_error(std::string("REPL error: ") + e.what());
            in_multiline = false;
            current_input.clear();
        }
    }
}

void REPL::stop() { }

} // namespace nv
