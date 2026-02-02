#include "frontend/interactive/interactive_session.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include <iostream>

namespace nv {
namespace interactive {

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
    
    return true;
}

} // namespace interactive
} // namespace nv
