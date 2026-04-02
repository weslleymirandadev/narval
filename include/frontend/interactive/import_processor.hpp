#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>

#include "frontend/ast/ast.hpp"
#include "frontend/interactive/repl_state.hpp"
#include "frontend/module_manager.hpp"

namespace nv {

// API comum para processamento de importações
class ImportProcessor {
public:
    // Processa importações em um input e modifica o AST para incluir os símbolos importados
    static bool process_imports(
        const std::string& input,
        std::unique_ptr<Node>& ast,
        REPLState* state,
        ModuleManager& module_manager
    );

private:
    // Extrai informações de importação do input
    static std::map<std::string, std::vector<std::string>> extract_imports(const std::string& input);
    
    // Verifica se o input contém importações
    static bool has_imports(const std::string& input);
    
    // Processa uma única importação
    static bool process_single_import(
        const std::string& module_path,
        const std::vector<std::string>& imported_names,
        std::vector<std::unique_ptr<Stmt>>& import_statements,
        REPLState* state,
        ModuleManager& module_manager
    );
    
    // Função auxiliar para extrair identifiers de um nó
    static std::set<std::string> extract_identifiers(const Node* node);
};

} // namespace nv
