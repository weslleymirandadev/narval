#include "frontend/interactive/interactive.hpp"
#include "frontend/ast/program.hpp"
#include "frontend/ast/expressions/call_expr_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/access_expr_node.hpp"
#include "frontend/ast/expressions/member_expr_node.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/ast/expressions/unary_minus_expr_node.hpp"
#include "frontend/ast/expressions/logical_not_expr_node.hpp"
#include "frontend/ast/expressions/array_expr_node.hpp"
#include "frontend/ast/expressions/tuple_expr_node.hpp"
#include "frontend/ast/expressions/vector_expr_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/ast/expressions/increment_expr_node.hpp"
#include "frontend/ast/expressions/decrement_expr_node.hpp"
#include "frontend/ast/expressions/post_increment_expr_node.hpp"
#include "frontend/ast/expressions/post_decrement_expr_node.hpp"
#include "frontend/ast/statements/declaration_stmt_node.hpp"
#include "frontend/ast/statements/def_stmt_node.hpp"
#include "frontend/checker/type.hpp"
#include "backend/codegen/ir_utils.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <llvm/TargetParser/Host.h>
#include <dlfcn.h>

// Incluir funções do runtime
#include "backend/runtime/nv_runtime.h"
#include "backend/runtime/prototypes.h"

// Definição para NARVAL_SOURCE_DIR se não existir
#ifndef NARVAL_SOURCE_DIR
#define NARVAL_SOURCE_DIR "/home/bacal/projects/cpp/narval"
#endif

extern "C" {
    // Funções do runtime que precisam ser acessíveis pelo JIT
    void nv_write(void* v);
    void create_str(void* out, const char* s);
    void create_int(void* out, int32_t v);
    void create_float(void* out, double v);
    void create_bool(void* out, int b);
}

#ifdef HAVE_READLINE
extern "C" {
    char* rl_completer(const char*, int);
    char** rl_completion(const char*, int, int);
}
#endif

namespace nv {

// =============================================================================
// Implementação de REPLState
// =============================================================================

REPLState::REPLState() 
    : llvm_context(std::make_unique<llvm::LLVMContext>()),
      module(std::make_unique<llvm::Module>("narval_repl", *llvm_context)),
      builder(std::make_unique<llvm::IRBuilder<llvm::NoFolder>>(*llvm_context)),
      checker(std::make_unique<Checker>()) {
}

REPLState::~REPLState() = default;

bool REPLState::initialize() {
    try {
        // Inicializar LLVM targets
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
        
        // Criar JIT
        auto jit_expected = llvm::orc::LLJITBuilder().create();
        if (!jit_expected) {
            llvm::Error err = jit_expected.takeError();
            std::cerr << "Failed to create JIT" << std::endl;
            llvm::consumeError(std::move(err));
            return false;
        }
        jit = std::move(*jit_expected);
        
        // Registrar funções do runtime no JIT
        register_runtime_functions();
        
        // Inicializar o checker com escopo global
        checker->push_scope();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during REPL initialization: " << e.what() << std::endl;
        return false;
    }
}

// Lista de símbolos do runtime que o codegen pode referenciar (nv_runtime.h + generate_ir.cpp)
static const char* const RUNTIME_SYMBOLS[] = {
    "nv_write", "nv_write_no_nl",
    "create_int", "create_float", "create_bool", "create_str",
    "create_array", "create_vector", "create_map",
    "ensure_value_type", "nv_read",
    "string_to_upper_case", "string_replace", "string_includes",
    "vector_push_method", "vector_pop_method", "vector_get_method", "vector_set_method",
    "array_get_index_v", "array_set_index_v",
    "tuple_get_impl", "tuple_set_impl",
    "json_load",
    nullptr
};

void REPLState::register_runtime_functions() {
    // Determinar caminho do runtime
    std::string runtime_path;
    const char* narval_home = std::getenv("NARVAL_HOME");
    if (narval_home) {
        runtime_path = std::string(narval_home) + "/runtime.so";
    } else {
        std::string dev_runtime = std::string(NARVAL_SOURCE_DIR) + "/build/lib/runtime.so";
        std::ifstream check_file(dev_runtime);
        if (check_file.good()) {
            runtime_path = dev_runtime;
            std::cout << "Using development runtime from: " << runtime_path << std::endl;
        } else {
            runtime_path = "/usr/lib/narval/runtime.so";
            std::cout << "Using production runtime from: " << runtime_path << std::endl;
        }
        check_file.close();
    }
    
    void* runtime_handle = dlopen(runtime_path.c_str(), RTLD_LAZY);
    if (!runtime_handle) {
        std::cerr << "Failed to load runtime from " << runtime_path << ": " << dlerror() << std::endl;
        return;
    }
    std::cout << "Loaded runtime from: " << runtime_path << std::endl;
    
    for (const char* const* p = RUNTIME_SYMBOLS; *p; ++p) {
        const char* name = *p;
        void* ptr = dlsym(runtime_handle, name);
        if (ptr) {
            symbols[name] = static_cast<llvm::JITTargetAddress>(reinterpret_cast<uintptr_t>(ptr));
        }
    }
    
    // Fazer o JIT resolver esses símbolos: definir no MainJITDylib como absolute symbols
    llvm::orc::SymbolMap sym_map;
    auto& es = jit->getExecutionSession();
    for (const auto& [name, addr] : symbols) {
        sym_map[es.intern(name)] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr(addr),
            llvm::JITSymbolFlags::Exported);
    }
    if (auto err = jit->getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(sym_map)))) {
        std::cerr << "Failed to define runtime symbols in JIT" << std::endl;
        llvm::consumeError(std::move(err));
    }
    
    // Símbolos do processo (atoi, strlen, etc.) para o JIT resolver
    auto& dl = jit->getDataLayout();
    auto generator = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
        dl.getGlobalPrefix()
    );
    if (generator) {
        jit->getMainJITDylib().addGenerator(std::move(*generator));
    } else {
        llvm::consumeError(generator.takeError());
    }
}

void REPLState::reset() {
    symbols.clear();
    repl_global_names.clear();
    repl_var_values.clear();
    repl_globals_added.clear();
    result_counter = 0;
    
    llvm_context = std::make_unique<llvm::LLVMContext>();
    module = std::make_unique<llvm::Module>("narval_repl", *llvm_context);
    builder = std::make_unique<llvm::IRBuilder<llvm::NoFolder>>(*llvm_context);
    
    checker = std::make_unique<Checker>();
    checker->push_scope();
    
    auto jit_expected = llvm::orc::LLJITBuilder().create();
    if (jit_expected) {
        jit = std::move(*jit_expected);
        register_runtime_functions();
    }
}

bool REPLState::add_symbol(const std::string& name, llvm::JITTargetAddress addr) {
    symbols[name] = addr;
    return true;
}

std::optional<llvm::JITTargetAddress> REPLState::get_symbol(const std::string& name) {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
        return it->second;
    }
    
    // Tentar obter do JIT
    if (jit) {
        if (auto symbol = jit->lookup(name)) {
            return symbol->getValue();
        }
    }
    
    return std::nullopt;
}

// =============================================================================
// Implementação de CommandParser
// =============================================================================

std::pair<REPLCommand, std::vector<std::string>> CommandParser::parse(const std::string& input) {
    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd;
    
    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg) {
        args.push_back(arg);
    }
    
    if (cmd == ":help" || cmd == ":h") {
        return {REPLCommand::HELP, args};
    } else if (cmd == ":quit" || cmd == ":exit" || cmd == ":q") {
        return {REPLCommand::QUIT, args};
    } else if (cmd == ":reset" || cmd == ":clear") {
        return {REPLCommand::RESET, args};
    } else if (cmd == ":vars" || cmd == ":v") {
        return {REPLCommand::VARS, args};
    } else if (cmd == ":history" || cmd == ":hist") {
        return {REPLCommand::HISTORY, args};
    } else if (cmd == ":load") {
        return {REPLCommand::LOAD, args};
    } else if (cmd == ":save") {
        return {REPLCommand::SAVE, args};
    }
    
    return {REPLCommand::NONE, args};
}

// =============================================================================
// Implementação de utilitários
// =============================================================================

namespace repl_utils {

bool is_brace_balanced(const std::string& input) {
    int parentheses = 0;
    int brackets = 0;
    int braces = 0;
    
    for (char c : input) {
        switch (c) {
            case '(': parentheses++; break;
            case ')': parentheses--; break;
            case '[': brackets++; break;
            case ']': brackets--; break;
            case '{': braces++; break;
            case '}': braces--; break;
        }
        
        if (parentheses < 0 || brackets < 0 || braces < 0) {
            return false;
        }
    }
    
    return parentheses == 0 && brackets == 0 && braces == 0;
}

bool ends_with_operator(const std::string& input) {
    // Remover whitespace do final
    size_t end = input.find_last_not_of(" \t\n\r");
    if (end == std::string::npos) return false;
    
    std::string trimmed = input.substr(0, end + 1);
    
    // Verificar se termina com operador que indica continuação
    std::vector<std::string> operators = {"+", "-", "*", "/", "=", "+=", "-=", "*=", "/=", "&&", "||", "<", ">", "<=", ">=", "!=", "==", "->", ".", ","};
    
    for (const auto& op : operators) {
        if (trimmed.length() >= op.length() && 
            trimmed.substr(trimmed.length() - op.length()) == op) {
            return true;
        }
    }
    
    return false;
}

bool needs_continuation(const std::string& input) {
    bool in_string = false;
    bool escape_next = false;
    
    for (char c : input) {
        if (escape_next) {
            escape_next = false;
            continue;
        }
        
        if (c == '\\') {
            escape_next = true;
            continue;
        }
        
        if (c == '"' && !escape_next) {
            in_string = !in_string;
        }
    }
    
    return in_string || escape_next;
}

std::string format_value(llvm::JITTargetAddress addr) {
    // Implementação simplificada - tentar interpretar como diferentes tipos
    // Na prática, precisaríamos de informações de tipo do runtime
    std::ostringstream oss;
    oss << "0x" << std::hex << addr;
    return oss.str();
}

std::string format_type(std::shared_ptr<Type> type) {
    if (!type) return "unknown";
    return type->toString();
}

bool is_valid_identifier(const std::string& name) {
    if (name.empty()) return false;
    
    if (!std::isalpha(name[0]) && name[0] != '_') return false;
    
    for (char c : name) {
        if (!std::isalnum(c) && c != '_') return false;
    }
    
    return true;
}

std::string sanitize_input(const std::string& input) {
    std::string result;
    bool in_string = false;
    bool escape_next = false;
    
    for (char c : input) {
        if (escape_next) {
            escape_next = false;
            result += c;
            continue;
        }
        
        if (c == '\\') {
            escape_next = true;
            result += c;
            continue;
        }
        
        if (c == '"' && !escape_next) {
            in_string = !in_string;
            result += c;
            continue;
        }
        
        // Remover comentários apenas fora de strings
        if (!in_string && c == '#') {
            break;
        }
        
        result += c;
    }
    
    // Remover whitespace extra do final
    result.erase(result.find_last_not_of(" \t\n\r") + 1);
    
    return result;
}

} // namespace repl_utils

// =============================================================================
// Implementação principal do REPL
// =============================================================================

REPL::REPL(const REPLConfig& config) : config(config), state(std::make_unique<REPLState>()) {
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
    std::string line;
    
#ifdef HAVE_READLINE
    if (config.enable_readline) {
        char* raw_line = readline(in_multiline ? config.multiline_prompt.c_str() : config.prompt.c_str());
        if (!raw_line) {
            return "";  // EOF
        }
        
        line = raw_line;
        free(raw_line);
        
        if (!line.empty()) {
            add_history(line.c_str());
        }
    } else {
#endif
        if (config.show_prompt) {
            std::cout << (in_multiline ? config.multiline_prompt : config.prompt);
            std::cout.flush();
        }
        
        std::getline(std::cin, line);
#ifdef HAVE_READLINE
    }
#endif
    
    return line;
}

bool REPL::is_complete_expression(const std::string& input) {
    return repl_utils::is_brace_balanced(input) && 
           !repl_utils::ends_with_operator(input) &&
           !repl_utils::needs_continuation(input);
}

std::string REPL::preprocess_input(const std::string& input) {
    // Remover comentários e whitespace extra
    std::string result = repl_utils::sanitize_input(input);
    
    // Remover whitespace do final
    if (!result.empty()) {
        size_t last = result.find_last_not_of(" \t\n\r");
        if (last != std::string::npos)
            result.erase(last + 1);
        else
            result.clear();
    }
    
    // Verificar se é uma definição de função ou estrutura complexa
    bool is_function_def = result.find("def ") == 0 || result.find("\ndef ") != std::string::npos;
    bool has_braces = result.find('{') != std::string::npos;
    bool will_auto_print = should_auto_print(result);
    
    // Se não terminar com ; e não for definição de função com chaves, e não vai fazer auto-print, adicionar ;
    if (!result.empty() && result.back() != ';' && !is_function_def && !has_braces && !will_auto_print) {
        result += ";";
    }
    
    // Auto-print: não injetar write() no JIT; o valor será capturado via parâmetro de saída e impresso em C++
    if (will_auto_print) {
        // Deixar a expressão como está (ex.: "x", "3+5", "write(42)")
    }
    
    return result;
}

bool REPL::should_auto_print(const std::string& input) {
    if (!config.auto_print) return false;
    
    // Remover whitespace para análise
    std::string trimmed = input;
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return false;
    trimmed = trimmed.substr(start);
    size_t last = trimmed.find_last_not_of(" \t\n\r");
    if (last != std::string::npos) trimmed.erase(last + 1);
    
    if (trimmed.empty()) return false;
    
    // Não auto-print para comandos ou declarações/estruturas
    static const char* stmt_prefixes[] = {
        "def ", "let ", "if ", "for ", "while ", "loop ", "match ", "return ", "break ", "continue ", "import "
    };
    for (const char* p : stmt_prefixes) {
        if (trimmed.size() >= strlen(p) && trimmed.compare(0, strlen(p), p) == 0)
            return false;
    }
    
    // Auto-print para qualquer linha que pareça uma única expressão:
    // identificador, expressão aritmética, chamada de função (incl. write), etc.
    // Se há ';' no meio, é mais de um statement → não forçar auto-print (deixar como está)
    size_t semi = trimmed.find(';');
    if (semi != std::string::npos && semi < trimmed.size() - 1) {
        std::string after = trimmed.substr(semi + 1);
        after.erase(0, after.find_first_not_of(" \t\n\r"));
        if (!after.empty()) return false;  // múltiplos statements
    }
    return true;
}

std::string REPL::generate_result_var() {
    return state->last_result_var + std::to_string(state->result_counter++);
}

namespace {
// Coleta nomes definidos (LHS de atribuição, alvo de decl, nome de def) e usados (identificadores) na AST.
void collect_repl_names(Node* node,
    std::unordered_set<std::string>& defined,
    std::unordered_set<std::string>& used) {
    if (!node) return;
    if (auto* id = dynamic_cast<IdentifierNode*>(node)) {
        used.insert(id->symbol);
        return;
    }
    if (auto* assign = dynamic_cast<AssignmentExprNode*>(node)) {
        if (assign->target && assign->value) {
            if (auto* lid = dynamic_cast<IdentifierNode*>(assign->target.get()))
                defined.insert(lid->symbol);
            collect_repl_names(assign->target.get(), defined, used);
            collect_repl_names(assign->value.get(), defined, used);
        }
        return;
    }
    if (auto* decl = dynamic_cast<DeclarationStmtNode*>(node)) {
        if (decl->target) {
            if (auto* tid = dynamic_cast<IdentifierNode*>(decl->target.get()))
                defined.insert(tid->symbol);
            collect_repl_names(decl->target.get(), defined, used);
        }
        if (decl->value) collect_repl_names(decl->value.get(), defined, used);
        return;
    }
    if (auto* def = dynamic_cast<DefStmtNode*>(node)) {
        defined.insert(def->name);
        for (auto& p : def->parameters)
            for (auto& kv : p.parameter)
                collect_repl_names(nullptr, defined, used);  // param names not needed as "used" for globals
        for (auto& s : def->body)
            if (s) collect_repl_names(s.get(), defined, used);
        return;
    }
    if (auto* prog = dynamic_cast<Program*>(node)) {
        for (const auto& s : prog->get_statements())
            if (s) collect_repl_names(s.get(), defined, used);
        return;
    }
    if (auto* call = dynamic_cast<CallExprNode*>(node)) {
        if (call->caller) collect_repl_names(call->caller.get(), defined, used);
        for (auto& a : call->args)
            if (a) collect_repl_names(a.get(), defined, used);
        return;
    }
    if (auto* bin = dynamic_cast<BinaryExprNode*>(node)) {
        if (bin->left) collect_repl_names(bin->left.get(), defined, used);
        if (bin->right) collect_repl_names(bin->right.get(), defined, used);
        return;
    }
    if (auto* acc = dynamic_cast<AccessExprNode*>(node)) {
        if (acc->expr) collect_repl_names(acc->expr.get(), defined, used);
        if (acc->index) collect_repl_names(acc->index.get(), defined, used);
        return;
    }
    if (auto* mem = dynamic_cast<MemberExprNode*>(node)) {
        if (mem->object) collect_repl_names(mem->object.get(), defined, used);
        if (mem->property) collect_repl_names(mem->property.get(), defined, used);
        return;
    }
    if (auto* cond = dynamic_cast<ConditionalExprNode*>(node)) {
        if (cond->condition) collect_repl_names(cond->condition.get(), defined, used);
        if (cond->true_expr) collect_repl_names(cond->true_expr.get(), defined, used);
        if (cond->false_expr) collect_repl_names(cond->false_expr.get(), defined, used);
        return;
    }
    if (auto* un = dynamic_cast<UnaryMinusExprNode*>(node)) {
        if (un->operand) collect_repl_names(un->operand.get(), defined, used);
        return;
    }
    if (auto* notn = dynamic_cast<LogicalNotExprNode*>(node)) {
        if (notn->operand) collect_repl_names(notn->operand.get(), defined, used);
        return;
    }
    if (auto* arr = dynamic_cast<ArrayExprNode*>(node)) {
        for (auto& e : arr->elements)
            if (e) collect_repl_names(e.get(), defined, used);
        return;
    }
    if (auto* tup = dynamic_cast<TupleExprNode*>(node)) {
        for (auto& e : tup->elements)
            if (e) collect_repl_names(e.get(), defined, used);
        return;
    }
    if (auto* vec = dynamic_cast<VectorExprNode*>(node)) {
        for (auto& e : vec->elements)
            if (e) collect_repl_names(e.get(), defined, used);
        return;
    }
    if (auto* rng = dynamic_cast<RangeExprNode*>(node)) {
        if (rng->start) collect_repl_names(rng->start.get(), defined, used);
        if (rng->end) collect_repl_names(rng->end.get(), defined, used);
        return;
    }
    if (auto* inc = dynamic_cast<IncrementExprNode*>(node)) {
        if (inc->operand) collect_repl_names(inc->operand.get(), defined, used);
        return;
    }
    if (auto* dec = dynamic_cast<DecrementExprNode*>(node)) {
        if (dec->operand) collect_repl_names(dec->operand.get(), defined, used);
        return;
    }
    if (auto* posti = dynamic_cast<PostIncrementExprNode*>(node)) {
        if (posti->operand) collect_repl_names(posti->operand.get(), defined, used);
        return;
    }
    if (auto* postd = dynamic_cast<PostDecrementExprNode*>(node)) {
        if (postd->operand) collect_repl_names(postd->operand.get(), defined, used);
        return;
    }
}

} // namespace

bool REPL::compile_and_execute(const std::string& input) {
    try {
        // 1. Tokenização
        Lexer lexer(input, "repl_line_001");
        auto tokens = lexer.tokenize();
        if (tokens.empty()) {
            return true;  // Input vazio é válido
        }
        
        // 2. Parsing
        Parser parser;
        auto ast = parser.produce_ast(tokens);
        if (parser.has_error()) {
            handle_error("Syntax error in input");
            return false;
        }
        
        // Verificar se a única instrução é write(...) para evitar imprimir duas vezes
        bool single_write_call = false;
        if (auto* prog = dynamic_cast<Program*>(ast.get())) {
            const auto& stmts = prog->get_statements();
            if (stmts.size() == 1) {
                if (auto* call = dynamic_cast<CallExprNode*>(stmts[0].get())) {
                    if (auto* id = dynamic_cast<IdentifierNode*>(call->caller.get())) {
                        if (id->symbol == "write") single_write_call = true;
                    }
                }
            }
        }
        
        // 3. Type checking
        state->checker->set_source_file("repl_line_001");
        auto& type = state->checker->check_node(ast.get());
        if (state->checker->err) {
            handle_error("Type error in input");
            return false;
        }
        
        std::unordered_set<std::string> defined_this_line, used_this_line;
        collect_repl_names(ast.get(), defined_this_line, used_this_line);
        
        // 4. Criar módulo temporário para esta expressão
        auto temp_module = std::make_unique<llvm::Module>("repl_expr", *state->llvm_context);
        auto temp_builder = std::make_unique<llvm::IRBuilder<llvm::NoFolder>>(*state->llvm_context);
        
        IRGenerationContext context(*state->llvm_context, *temp_module, *temp_builder, state->checker.get());
        
        auto& C = *state->llvm_context;
        auto* ValueTy = llvm::StructType::getTypeByName(C, "nv.rt.Value");
        if (!ValueTy) {
            auto* i32 = llvm::Type::getInt32Ty(C);
            auto* i64 = llvm::Type::getInt64Ty(C);
            auto* i8  = llvm::Type::getInt8Ty(C);
            auto* i8p = llvm::PointerType::getUnqual(i8);
            ValueTy = llvm::StructType::create(C, {i32, i64, i8p, i8p, i32}, "nv.rt.Value");
        }
        auto* VoidTy  = llvm::Type::getVoidTy(C);
        auto* ValuePtr= llvm::PointerType::getUnqual(ValueTy);
        
        temp_module->getOrInsertFunction("nv_write", llvm::FunctionType::get(VoidTy, {ValuePtr}, false));
        temp_module->getOrInsertFunction("create_str", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::PointerType::getUnqual(llvm::Type::getInt8Ty(C))}, false));
        temp_module->getOrInsertFunction("create_int", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getInt32Ty(C)}, false));
        temp_module->getOrInsertFunction("create_float", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getDoubleTy(C)}, false));
        temp_module->getOrInsertFunction("create_bool", llvm::FunctionType::get(VoidTy, {ValuePtr, llvm::Type::getInt32Ty(C)}, false));
        
        // 5. Slots no host: variáveis REPL vivem em state->repl_var_values; o wrapper recebe Value** slots
        std::vector<std::string> slot_names;
        for (const std::string& name : defined_this_line) {
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (!nv_type) continue;
            nv_type = context.resolve_type(nv_type);
            if (nv_type->kind == nv::Kind::DEF) continue;
            slot_names.push_back(name);
            state->repl_var_values[name] = Value{};
        }
        for (const std::string& name : used_this_line) {
            if (defined_this_line.count(name)) continue;
            if (!state->repl_global_names.count(name)) continue;
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (!nv_type) continue;
            nv_type = context.resolve_type(nv_type);
            if (nv_type->kind == nv::Kind::DEF) {
                auto* def = std::static_pointer_cast<nv::Def>(nv_type).get();
                if (!def) continue;
                std::vector<llvm::Type*> param_tys;
                for (const auto& p : def->paramstype)
                    param_tys.push_back(context.nv_type_to_llvm(p));
                llvm::Type* ret_ty = context.nv_type_to_llvm(def->returntype);
                auto* ft = llvm::FunctionType::get(ret_ty, param_tys, false);
                auto ext_fn = temp_module->getOrInsertFunction(name, ft);
                llvm::Function* fn = llvm::cast<llvm::Function>(ext_fn.getCallee());
                nv::SymbolInfo info(fn, fn->getType(), nv_type, false, true);
                context.get_symbol_table().define_symbol(name, info);
            } else {
                if (std::find(slot_names.begin(), slot_names.end(), name) == slot_names.end())
                    slot_names.push_back(name);
                // Só garantir que a chave existe; não sobrescrever valor de linha anterior
                if (state->repl_var_values.find(name) == state->repl_var_values.end())
                    state->repl_var_values[name] = Value{};
            }
        }
        // Garantir que todas as variáveis REPL tenham slot (para sync slot->global antes de chamar funções que as usam)
        for (const std::string& name : state->repl_global_names) {
            if (std::find(slot_names.begin(), slot_names.end(), name) != slot_names.end()) continue;
            std::shared_ptr<nv::Type> nv_type;
            try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
            if (!nv_type) continue;
            nv_type = context.resolve_type(nv_type);
            if (nv_type->kind == nv::Kind::DEF) continue;  // só variáveis
            slot_names.push_back(name);
            if (state->repl_var_values.find(name) == state->repl_var_values.end())
                state->repl_var_values[name] = Value{};
        }
        
        // Globais no JIT para variáveis REPL (funções definidas em fragmentos anteriores leem o mesmo global)
        auto* I32 = llvm::Type::getInt32Ty(C);
        for (const std::string& name : slot_names) {
            if (state->repl_globals_added.count(name)) continue;
            auto glob_mod = std::make_unique<llvm::Module>("repl_globals_" + name, C);
            (void)new llvm::GlobalVariable(*glob_mod, ValueTy, false, llvm::GlobalValue::ExternalLinkage,
                llvm::Constant::getNullValue(ValueTy), "repl_global_" + name);
            auto tsm_glob = llvm::orc::ThreadSafeModule(std::move(glob_mod), std::make_unique<llvm::LLVMContext>());
            if (auto err = state->jit->addIRModule(std::move(tsm_glob))) {
                llvm::consumeError(std::move(err));
                continue;
            }
            state->repl_globals_added.insert(name);
        }
        
        static int expr_counter = 0;
        std::string func_name = "__repl_expr_" + std::to_string(expr_counter++);
        
        llvm::Type* slots_ty = llvm::PointerType::getUnqual(ValuePtr);
        std::vector<llvm::Type*> param_tys = { ValuePtr };
        if (!slot_names.empty())
            param_tys.push_back(slots_ty);
        auto* func_type_w = llvm::FunctionType::get(VoidTy, param_tys, false);
        auto* func = llvm::Function::Create(func_type_w, llvm::Function::ExternalLinkage, func_name, temp_module.get());
        func->getArg(0)->setName("out_result");
        llvm::Value* out_param = func->getArg(0);
        llvm::Value* slots_param = slot_names.empty() ? nullptr : func->getArg(1);
        
        auto* entry_bb = llvm::BasicBlock::Create(C, "entry", func);
        temp_builder->SetInsertPoint(entry_bb);
        if (slots_param) {
            for (size_t i = 0; i < slot_names.size(); ++i) {
                const std::string& name = slot_names[i];
                llvm::Value* slot_addr = temp_builder->CreateGEP(ValuePtr, slots_param, llvm::ConstantInt::get(I32, i));
                llvm::Value* slot = temp_builder->CreateLoad(ValuePtr, slot_addr);
                // Global no JIT para esta variável (funções em outros fragmentos leem daqui)
                llvm::GlobalVariable* global_var = llvm::cast<llvm::GlobalVariable>(
                    temp_module->getOrInsertGlobal("repl_global_" + name, ValueTy));
                if (global_var->isDeclaration())
                    global_var->setLinkage(llvm::GlobalValue::ExternalLinkage);
                llvm::Value* global_ptr = temp_builder->CreatePointerCast(global_var, ValuePtr);
                // Entrada: slot (host) -> global (JIT)
                llvm::Value* val_from_host = temp_builder->CreateLoad(ValueTy, slot, name + "_load");
                temp_builder->CreateStore(val_from_host, global_var);
                std::shared_ptr<nv::Type> nv_type;
                try { nv_type = state->checker->scope->get_key(name); } catch (...) { continue; }
                if (nv_type) nv_type = context.resolve_type(nv_type);
                nv::SymbolInfo info(global_ptr, ValueTy, nv_type, false, false);
                context.get_symbol_table().define_symbol(name, info);
            }
        }
        context.set_current_function(func);
        context.enter_scope();
        
        nv::generate_ir(std::move(ast), context);
        
        // Saída: global (JIT) -> slot (host) para o host ter o valor atualizado
        if (slots_param) {
            for (size_t i = 0; i < slot_names.size(); ++i) {
                const std::string& name = slot_names[i];
                llvm::GlobalVariable* global_var = temp_module->getGlobalVariable("repl_global_" + name);
                if (!global_var) continue;
                llvm::Value* slot_addr = temp_builder->CreateGEP(ValuePtr, slots_param, llvm::ConstantInt::get(I32, i));
                llvm::Value* slot = temp_builder->CreateLoad(ValuePtr, slot_addr);
                llvm::Value* val_to_host = temp_builder->CreateLoad(ValueTy, global_var, name + "_out");
                temp_builder->CreateStore(val_to_host, slot);
            }
        }

        // Se a última instrução deixou um valor na pilha, gravar em *out_result (host imprime em C++)
        bool have_result = false;
        if (context.has_value()) {
            llvm::Value* result = context.pop_value();
            if (result && result->getType() == ValueTy) {
                llvm::Value* to_store = result;
                if (llvm::isa<llvm::AllocaInst>(result)) {
                    to_store = temp_builder->CreateLoad(ValueTy, result, "repl_result_load");
                }
                temp_builder->CreateStore(to_store, out_param);
                have_result = true;
            }
        }
        temp_builder->CreateRetVoid();
        
        // 6. Compilação JIT
        auto tsm = llvm::orc::ThreadSafeModule(std::move(temp_module), std::make_unique<llvm::LLVMContext>());
        if (auto err = state->jit->addIRModule(std::move(tsm))) {
            llvm::errs() << "JIT compilation failed: " << err << "\n";
            llvm::consumeError(std::move(err));
            handle_error("JIT compilation failed");
            return false;
        }
        
        // 7. Execução: passar out e array de slots (Value*) para o wrapper
        Value result_buffer = {};
        if (auto func_symbol = state->jit->lookup(func_name)) {
            void* addr = (void*)func_symbol->getValue();
            if (slot_names.empty()) {
                ((void(*)(Value*))addr)(&result_buffer);
            } else {
                std::vector<Value*> slot_ptrs;
                for (const auto& n : slot_names)
                    slot_ptrs.push_back(&state->repl_var_values[n]);
                ((void(*)(Value*, Value**))addr)(&result_buffer, slot_ptrs.data());
            }
        } else {
            handle_error("Failed to find compiled function");
            return false;
        }
        
        // 8. Auto-print em C++: usar nv_write do runtime com o valor capturado (sem depender de write() no JIT)
        if (have_result && !single_write_call) {
            auto sym = state->get_symbol("nv_write");
            if (sym) {
                print_value(llvm::JITTargetAddress(&result_buffer));
            } else {
                std::cout << "(value captured, nv_write not available)" << std::endl;
            }
        }
        
        for (const std::string& n : defined_this_line)
            state->repl_global_names.insert(n);
        
        return true;
        
    } catch (const std::exception& e) {
        handle_error("Compilation error: " + std::string(e.what()));
        return false;
    }
}

void REPL::print_value(llvm::JITTargetAddress addr) {
    // Imprimir em C++ com o valor capturado do JIT; usar nv_write do runtime já carregado
    auto sym = state->get_symbol("nv_write");
    if (sym) {
        using NvWriteFn = void(*)(Value*);
        (*reinterpret_cast<NvWriteFn>(*sym))(reinterpret_cast<Value*>(addr));
    }
}

bool REPL::execute_command(REPLCommand cmd, const std::vector<std::string>& args) {
    switch (cmd) {
        case REPLCommand::HELP:
            show_help();
            return true;
            
        case REPLCommand::QUIT:
        case REPLCommand::EXIT:
            return false;  // Sair do loop
            
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
                handle_error("Usage: :load <filename>");
                return true;
            }
            return load_file(args[0]);
            
        case REPLCommand::SAVE:
            if (args.empty()) {
                handle_error("Usage: :save <filename>");
                return true;
            }
            return save_history(args[0]);
            
        default:
            return true;
    }
}

void REPL::show_help() {
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

void REPL::show_variables() {
    std::cout << "\nDefined variables:\n";
    for (const auto& [name, addr] : state->symbols) {
        std::cout << "  " << name << " = " << repl_utils::format_value(addr) << std::endl;
    }
    std::cout << std::endl;
}

void REPL::show_history() {
    std::cout << "\nCommand history:\n";
    for (size_t i = 0; i < state->history.size(); ++i) {
        std::cout << "  " << (i + 1) << ": " << state->history[i] << std::endl;
    }
    std::cout << std::endl;
}

bool REPL::load_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        handle_error("Cannot open file: " + filename);
        return true;
    }
    
    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;
        if (!line.empty()) {
            std::cout << "Loading line " << line_num << ": " << line << std::endl;
            if (!compile_and_execute(line)) {
                handle_error("Error executing line " + std::to_string(line_num));
            }
        }
    }
    
    return true;
}

bool REPL::save_history(const std::string& filename) {
    std::ofstream file(filename);
    if (!file) {
        handle_error("Cannot create file: " + filename);
        return true;
    }
    
    for (const auto& cmd : state->history) {
        file << cmd << std::endl;
    }
    
    std::cout << "History saved to " << filename << std::endl;
    return true;
}

void REPL::reset_state() {
    state->reset();
    std::cout << "REPL state reset" << std::endl;
}

void REPL::handle_error(const std::string& error) {
    if (config.show_errors) {
        std::cerr << "Error: " << error << std::endl;
    }
}

void REPL::handle_warning(const std::string& warning) {
    if (config.show_warnings) {
        std::cout << "Warning: " << warning << std::endl;
    }
}

void REPL::add_to_history(const std::string& command) {
    state->history.push_back(command);
    
    // Limitar tamanho do histórico
    if (state->history.size() > static_cast<size_t>(config.max_history)) {
        state->history.erase(state->history.begin());
    }
}

void REPL::run() {
    while (true) {
        try {
            std::string line = read_input();
            
            // EOF detection
            if (line.empty() && std::cin.eof()) {
                std::cout << std::endl;
                break;
            }
            
            // Skip empty lines
            if (line.empty()) {
                continue;
            }
            
            // Check for commands
            if (line[0] == ':') {
                auto [cmd, args] = CommandParser::parse(line);
                if (cmd != REPLCommand::NONE) {
                    if (!execute_command(cmd, args)) {
                        break;  // Command requested exit
                    }
                    continue;
                }
            }
            
            // Handle multiline input
            if (in_multiline) {
                current_input += "\n" + line;
            } else {
                current_input = line;
            }
            
            // Check if input is complete
            if (!is_complete_expression(current_input)) {
                in_multiline = true;
                continue;
            }
            
            // Reset multiline flag and process
            in_multiline = false;
            
            // Add to history
            add_to_history(current_input);
            
            // Preprocess and execute
            std::string processed = preprocess_input(current_input);
            compile_and_execute(processed);
            
            current_input.clear();
            
        } catch (const std::exception& e) {
            handle_error("REPL error: " + std::string(e.what()));
            in_multiline = false;
            current_input.clear();
        }
    }
}

void REPL::stop() {
    // Cleanup if needed
}

} // namespace nv
