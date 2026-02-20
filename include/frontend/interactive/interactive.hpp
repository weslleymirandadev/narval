#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <optional>

// LLVM includes
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/InitLLVM.h>

// Narval includes
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/checker/checker.hpp"
#include "backend/codegen/ir_context.hpp"
#include "backend/codegen/generate_ir.hpp"
#include "backend/runtime/prototypes.h"

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace nv {

/**
 * Configuração do REPL
 */
struct REPLConfig {
    bool enable_readline = true;
    bool show_prompt = true;
    bool show_errors = true;
    bool show_warnings = true;
    bool auto_print = true;
    std::string prompt = "nv> ";
    std::string multiline_prompt = "... ";
    int max_history = 1000;
    
    // Configurações de compilação JIT
    int optimization_level = 1;  // 0-3, onde 3 é máximo
    bool enable_debug = false;
};

/**
 * Estado compartilhado entre sessões do REPL
 */
struct REPLState {
    // Contexto LLVM persistente
    std::unique_ptr<llvm::LLVMContext> llvm_context;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::IRBuilder<llvm::NoFolder>> builder;
    
    // Checker de tipos com escopo persistente
    std::unique_ptr<Checker> checker;
    
    // JIT engine
    std::unique_ptr<llvm::orc::LLJIT> jit;
    
    // Símbolos definidos nas sessões anteriores
    std::unordered_map<std::string, llvm::JITTargetAddress> symbols;
    
    // Histórico de comandos
    std::vector<std::string> history;
    
    // Variáveis para auto-print
    std::string last_result_var = "_";
    int result_counter = 0;
    
    // Nomes definidos em linhas anteriores (variáveis e funções) para persistir entre fragmentos JIT
    std::unordered_set<std::string> repl_global_names;
    
    // Valores das variáveis REPL no host (JIT escreve/lê via slots passados como parâmetro)
    std::unordered_map<std::string, Value> repl_var_values;
    // Nomes para os quais já existe um global no JIT (repl_global_<name>) para funções verem x entre linhas
    std::unordered_set<std::string> repl_globals_added;
    
    REPLState();
    ~REPLState();
    
    bool initialize();
    void reset();
    void register_runtime_functions();
    bool add_symbol(const std::string& name, llvm::JITTargetAddress addr);
    std::optional<llvm::JITTargetAddress> get_symbol(const std::string& name);
};

/**
 * Comandos internos do REPL
 */
enum class REPLCommand {
    HELP,
    QUIT,
    EXIT,
    RESET,
    VARS,
    HISTORY,
    LOAD,
    SAVE,
    CLEAR,
    NONE
};

/**
 * Parser de comandos internos
 */
class CommandParser {
public:
    static std::pair<REPLCommand, std::vector<std::string>> parse(const std::string& input);
};

/**
 * Implementação principal do REPL
 */
class REPL {
private:
    REPLConfig config;
    std::unique_ptr<REPLState> state;
    
    // Input handling
    std::string current_input;
    bool in_multiline = false;
    
    // Métodos internos
    std::string read_input();
    bool is_complete_expression(const std::string& input);
    std::string preprocess_input(const std::string& input);
    
    // Compilação e execução
    bool compile_and_execute(const std::string& input);
    bool should_auto_print(const std::string& input);
    void print_value(llvm::JITTargetAddress addr);
    
    // Comandos internos
    bool execute_command(REPLCommand cmd, const std::vector<std::string>& args);
    void show_help();
    void show_variables();
    void show_history();
    bool load_file(const std::string& filename);
    bool save_history(const std::string& filename);
    void reset_state();
    
    // Tratamento de erros
    void handle_error(const std::string& error);
    void handle_warning(const std::string& warning);
    
    // Utilitários
    std::string generate_result_var();
    void add_to_history(const std::string& command);
    
public:
    explicit REPL(const REPLConfig& config = REPLConfig{});
    ~REPL();
    
    // Inicialização e execução
    bool initialize();
    void run();
    void stop();
    
    // Acesso ao estado (para testes e extensões)
    REPLState* get_state() { return state.get(); }
};

/**
 * Funções utilitárias para o REPL
 */
namespace repl_utils {
    // Detecção de expressões completas
    bool is_brace_balanced(const std::string& input);
    bool ends_with_operator(const std::string& input);
    bool needs_continuation(const std::string& input);
    
    // Formatação de saída
    std::string format_value(llvm::JITTargetAddress addr);
    std::string format_type(std::shared_ptr<Type> type);
    
    // Validação
    bool is_valid_identifier(const std::string& name);
    std::string sanitize_input(const std::string& input);
}

} // namespace nv
