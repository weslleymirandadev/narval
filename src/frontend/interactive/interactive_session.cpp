#if 0

#include "frontend/interactive/interactive_session.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include <iostream>
#include <cstring>
#include <cstdlib>

// Runtime headers for Value and helpers
extern "C" {
#include "backend/runtime/prototypes.h"
#include "backend/runtime/nv_runtime.h"
}

namespace nv {
namespace interactive {

// Queue to store function returns registered from JIT'd code
struct FunctionReturnEntry {
    void* return_value;
    char function_name[256];
};

struct FunctionReturnQueue {
    size_t count;
    size_t last_printed;
    FunctionReturnEntry entries[1024];
    FunctionReturnQueue() : count(0), last_printed(0) { std::memset(entries, 0, sizeof(entries)); }
};

static FunctionReturnQueue return_queue;

// Stack to track variable/expression values for REPL output
struct ValueStackEntry {
    void* value;
    char expression[256];
    char source_name[256];
    int line_number;
    ValueStackEntry() : value(nullptr), line_number(0) {
        std::memset(expression, 0, sizeof(expression));
        std::memset(source_name, 0, sizeof(source_name));
    }
};

struct ValueStack {
    size_t count;
    ValueStackEntry entries[512];
    ValueStack() : count(0) { std::memset(entries, 0, sizeof(entries)); }
    
    void push(void* value, const char* expr, const char* source, int line = 0) {
        if (count >= 512) return;
        entries[count].value = value;
        if (expr) {
            std::strncpy(entries[count].expression, expr, 255);
            entries[count].expression[255] = '\0';
        }
        if (source) {
            std::strncpy(entries[count].source_name, source, 255);
            entries[count].source_name[255] = '\0';
        }
        entries[count].line_number = line;
        count++;
    }
    
    ValueStackEntry* pop() {
        if (count == 0) return nullptr;
        return &entries[--count];
    }
    
    void clear() {
        for (size_t i = 0; i < count; ++i) {
            if (entries[i].value) {
                free_value(static_cast<::Value*>(entries[i].value));
                std::free(entries[i].value);
                entries[i].value = nullptr;
            }
        }
        count = 0;
    }
};

static ValueStack value_stack;

// C bridge called from generated code to register a top-level function return
extern "C" void nv_register_function_return(Value* return_value, const char* function_name) {
    if (!return_value) {
        fprintf(stderr, "[nv_register_function_return] WARNING: return_value is null\n");
        return;
    }
    if (return_queue.count >= 1024) {
        fprintf(stderr, "[nv_register_function_return] WARNING: queue full\n");
        return;
    }
    
    // Copy incoming Value to heap so it outlives the caller's stack
    Value* copy = (Value*)std::malloc(sizeof(Value));
    if (!copy) {
        fprintf(stderr, "[nv_register_function_return] ERROR: malloc failed\n");
        return;
    }
    std::memcpy(copy, return_value, sizeof(Value));
    return_queue.entries[return_queue.count].return_value = copy;
    if (function_name) {
        std::strncpy(return_queue.entries[return_queue.count].function_name, function_name, 255);
        return_queue.entries[return_queue.count].function_name[255] = '\0';
    } else {
        return_queue.entries[return_queue.count].function_name[0] = '\0';
    }
    // Debug: log registration (temporary)
    fprintf(stderr, "[nv_register_function_return] queued index=%zu name=%s ptr=%p\n", return_queue.count, return_queue.entries[return_queue.count].function_name[0] ? return_queue.entries[return_queue.count].function_name : "(null)", (void*)copy);
    ++return_queue.count;
}

// C bridge called from generated code to register variable/expression values for REPL output
extern "C" void nv_register_value(Value* value, const char* expression, const char* source_name) {
    if (!value) return;
    if (value_stack.count >= 512) return;
    
    // Copy incoming Value to heap so it outlives the caller's stack
    Value* copy = (Value*)std::malloc(sizeof(Value));
    if (!copy) return;
    std::memcpy(copy, value, sizeof(Value));
    
    value_stack.push(copy, expression, source_name);
    
    // Debug: log registration
    fprintf(stderr, "[nv_register_value] registered expr=%s source=%s ptr=%p stack_count=%zu\n", 
            expression ? expression : "(null)", 
            source_name ? source_name : "(null)", 
            (void*)copy, 
            value_stack.count);
}

// C bridge called from write() function to register its argument for REPL output
extern "C" void nv_register_write_value(Value* value) {
    if (!value) return;
    if (value_stack.count >= 512) return;
    
    // Copy incoming Value to heap so it outlives the caller's stack
    Value* copy = (Value*)std::malloc(sizeof(Value));
    if (!copy) return;
    std::memcpy(copy, value, sizeof(Value));
    
    value_stack.push(copy, "write()", "write_call");
}

// LLVMContext compartilhado para toda a sessão
static llvm::LLVMContext& get_shared_llvm_context() {
    static llvm::LLVMContext context;
    return context;
}

InteractiveSession::InteractiveSession(Mode mode)
    : mode(mode)
    , incremental_checker(session_manager)
    , ir_builder(get_shared_llvm_context())
    , last_result(nullptr)
    , last_has_errors(false) {
    
    // Configurar session manager no IR builder
    ir_builder.set_session_manager(&session_manager);
    
    // Inicializar epoch system apenas para modo NOTEBOOK
    if (mode == Mode::NOTEBOOK) {
        epoch_system = std::make_unique<EpochSystem>(session_manager);
    }
    
    // Inicializar JIT engine
    if (!jit_engine.initialize()) {
        std::cerr << "Warning: Failed to initialize JIT engine\n";
    }
}

bool InteractiveSession::execute_repl(const std::string& source, const std::string& source_name) {
    if (mode != Mode::REPL) {
        std::cerr << "Error: Session is not in REPL mode\n";
        return false;
    }
    
    // Parse código fonte
    auto ast = parse_source(source, source_name);
    if (!ast) {
        last_has_errors = true;
        return false;
    }
    
    // Criar unidade de execução
    ExecutionUnitId unit_id = session_manager.create_unit_id();
    auto unit = std::make_unique<ExecutionUnit>(unit_id, std::move(ast), source_name);
    
    // Executar unidade
    return execute_unit(std::move(unit));
}

bool InteractiveSession::execute_notebook_cell(const std::string& cell_id, const std::string& source) {
    if (mode != Mode::NOTEBOOK) {
        std::cerr << "Error: Session is not in NOTEBOOK mode\n";
        return false;
    }
    
    if (!epoch_system) {
        std::cerr << "Error: Epoch system not initialized\n";
        return false;
    }
    
    // Parse código fonte
    auto ast = parse_source(source, cell_id);
    if (!ast) {
        last_has_errors = true;
        return false;
    }
    
    // Criar unidade de execução
    ExecutionUnitId unit_id = session_manager.create_unit_id();
    auto unit = std::make_unique<ExecutionUnit>(unit_id, std::move(ast), cell_id);
    
    // Executar via epoch system
    epoch_system->execute_cell(cell_id, unit_id);
    
    // Executar unidade
    bool success = execute_unit(std::move(unit));
    
    return success;
}

bool InteractiveSession::reexecute_notebook_cell(const std::string& cell_id, const std::string& source) {
    if (mode != Mode::NOTEBOOK) {
        std::cerr << "Error: Session is not in NOTEBOOK mode\n";
        return false;
    }
    
    if (!epoch_system) {
        std::cerr << "Error: Epoch system not initialized\n";
        return false;
    }
    
    // Parse código fonte
    auto ast = parse_source(source, cell_id);
    if (!ast) {
        last_has_errors = true;
        return false;
    }
    
    // Criar nova unidade de execução
    ExecutionUnitId unit_id = session_manager.create_unit_id();
    auto unit = std::make_unique<ExecutionUnit>(unit_id, std::move(ast), cell_id);
    
    // Reexecutar via epoch system (invalida dependências)
    auto result = epoch_system->reexecute_cell(cell_id, unit_id);
    
    // Invalidar IR das unidades afetadas
    for (ExecutionUnitId invalidated_id : result.invalidated_units) {
        ir_builder.invalidate_unit_ir(invalidated_id);
        jit_engine.remove_module(invalidated_id);
    }
    
    // Executar nova unidade
    bool success = execute_unit(std::move(unit));
    
    return success;
}

void InteractiveSession::clear() {
    // Limpar Session Manager
    session_manager.clear();
    
    // Limpar Epoch System (se existir)
    if (epoch_system) {
        epoch_system->clear();
    }
    
    // Limpar JIT Engine - remover todos os módulos registrados
    for (ExecutionUnitId unit_id : executed_units) {
        jit_engine.remove_module(unit_id);
    }
    
    // Limpar IR Builder - remover todos os módulos de unidades
    ir_builder.clear_unit_modules();
    
    // Limpar rastreamento de unidades
    executed_units.clear();
    
    // Resetar estado
    last_result = nullptr;
    last_has_errors = false;

    // Liberar quaisquer retornos registrados ainda não impressos
    for (size_t i = 0; i < return_queue.count; ++i) {
        void* rv = return_queue.entries[i].return_value;
        if (rv) {
            free_value(static_cast<::Value*>(rv));
            std::free(rv);
            return_queue.entries[i].return_value = nullptr;
        }
    }
    return_queue.count = 0;
    return_queue.last_printed = 0;
    
    // Limpar value stack
    value_stack.clear();
}

std::unique_ptr<Node> InteractiveSession::parse_source(const std::string& source, const std::string& source_name) {
    try {
        // Criar lexer
        Lexer lexer(source, source_name);
        auto tokens = lexer.tokenize();
        
        // Criar parser
        Parser parser;
        auto import_infos = lexer.get_import_infos();
        auto ast = parser.produce_ast(tokens, import_infos);
        
        return ast;
    } catch (const std::exception& e) {
        std::cerr << "Parse error: " << e.what() << "\n";
        return nullptr;
    }
}

bool InteractiveSession::execute_unit(std::unique_ptr<ExecutionUnit> unit) {
    if (!unit) {
        return false;
    }
    
    // Processar e executar
    return process_and_execute(*unit);
}

bool InteractiveSession::process_and_execute(ExecutionUnit& unit) {
    // 1. Análise semântica incremental
    if (!incremental_checker.check_unit(unit)) {
        last_has_errors = true;
        return false;
    }
    
    // 2. Gerar IR incremental
    auto& checker = incremental_checker.get_checker();
    auto module = ir_builder.build_unit_ir(unit, checker);
    if (!module) {
        last_has_errors = true;
        return false;
    }
    
    // 3. Adicionar módulo ao JIT
    if (!jit_engine.add_module(std::move(module), unit.get_id())) {
        last_has_errors = true;
        return false;
    }
    
    // Registrar unidade para limpeza futura
    executed_units.insert(unit.get_id());
    
    // 4. Executar código
    last_result = jit_engine.execute_unit(unit.get_id());
    last_has_errors = false;
    // Verificar e imprimir novos retornos de função top-level (apenas REPL)
    if (mode == Mode::REPL) {
        check_and_print_function_returns();
    }
    
    return true;
}

void InteractiveSession::check_and_print_function_returns() {
    using ::Value;
    
    // First, print values from the value stack (variables/expressions)
    while (value_stack.count > 0) {
        auto* entry = value_stack.pop();
        if (entry && entry->value) {
            std::cout << "<<< ";
            nv_write(static_cast<Value*>(entry->value));
            std::cout << "\n";
            
            // free runtime contents and buffer
            free_value(static_cast<Value*>(entry->value));
            std::free(entry->value);
            entry->value = nullptr;
        }
    }
    
    // Then, print function returns (existing logic)
    auto* q = &return_queue;
    for (size_t i = q->last_printed; i < q->count; ++i) {
        auto& e = q->entries[i];
        if (e.return_value) {
            std::cout << "<<< ";
            nv_write(static_cast<Value*>(e.return_value));
            std::cout << "\n";
            
            // free runtime contents and buffer
            free_value(static_cast<Value*>(e.return_value));
            std::free(e.return_value);
            e.return_value = nullptr;
        }
    }
    q->last_printed = q->count;
}

} // namespace interactive
} // namespace nv

#endif
