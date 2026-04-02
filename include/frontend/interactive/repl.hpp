#pragma once

#include <string>
#include <vector>
#include <memory>
#include <filesystem>

#include <llvm/ExecutionEngine/JITSymbol.h>

#include "frontend/ast/ast.hpp"
#include "frontend/interactive/repl_state.hpp"
#include "frontend/interactive/command_parser.hpp"
#include "frontend/interactive/repl_utils.hpp"
#include "frontend/module_manager.hpp"

// Forward declarations for the new modules
namespace nv {
    class InputProcessor;
    class CompilationEngine;
    class CommandHandler;
}

#ifndef NARVAL_SOURCE_DIR
#define NARVAL_SOURCE_DIR "/home/bacal/projects/cpp/narval"
#endif

namespace nv {

struct REPLConfig {
    bool enable_readline = true;
    bool show_prompt = true;
    bool show_errors = true;
    bool show_warnings = true;
    bool auto_print = true;
    std::string prompt = "nv> ";
    std::string multiline_prompt = "... ";
    int max_history = 1000;
    int optimization_level = 1;
    bool enable_debug = false;
};

class REPL {
public:
    explicit REPL(const REPLConfig& config = REPLConfig{});
    ~REPL();

    bool initialize();
    void run();
    void stop();

    // Execute a source snippet using the REPL machinery (public API for Notebook)
    bool execute_source(const std::string& src);

    REPLState* get_state();

private:
    // Core state
    std::unique_ptr<REPLState> state;
    REPLConfig config;
    bool stop_flag = false;
    
    // Module manager for handling imports
    ModuleManager module_manager;

    // New modular components
    std::unique_ptr<InputProcessor> input_processor;
    std::unique_ptr<CompilationEngine> compilation_engine;
    std::unique_ptr<CommandHandler> command_handler;

    // Multiline input state
    bool in_multiline = false;
    std::string current_input;

    // Helpers (implemented in src/frontend/interactive/repl.cpp)
    std::string read_input();
    bool is_complete_expression(const std::string& input);
    std::string preprocess_input(const std::string& input);
    bool should_auto_print(const std::string& s);
    std::string generate_result_var();
    void add_to_history(const std::string& command);
    void print_value(llvm::JITTargetAddress addr);
    bool execute_command(REPLCommand cmd, const std::vector<std::string>& args);
    
    // Delegated methods to command handler
    void show_help();
    void show_variables();
    void show_history();
    bool load_file(const std::string& filename);
    bool save_history(const std::string& filename);
    void reset_state();
    void handle_error(const std::string& error);
    void handle_warning(const std::string& warning);
};

// Função compartilhada para processar imports recursivamente
bool process_imports_recursive(const std::string& input, std::unique_ptr<Node>& ast, REPLState* state, ModuleManager& module_manager);

} // namespace nv
