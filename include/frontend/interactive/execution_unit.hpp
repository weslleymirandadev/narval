#pragma once

#include "frontend/ast/ast.hpp"
#include "frontend/interactive/session_manager.hpp"
#include <string>
#include <memory>
#include <unordered_set>
#include <vector>

namespace nv {
namespace interactive {

/**
 * Unidade de Execução Incremental
 * 
 * Representa uma entrada no modo interativo:
 * - Um comando REPL (linha a linha)
 * - Uma célula de notebook
 * 
 * Cada unidade possui:
 * - ID único
 * - AST próprio
 * - Referência ao contexto da sessão
 * - Lista de símbolos definidos
 * - Lista de símbolos usados
 */
class ExecutionUnit {
public:
    ExecutionUnit(ExecutionUnitId id, 
                  std::unique_ptr<Node> ast,
                  const std::string& source_name);
    
    ~ExecutionUnit() = default;
    
    // Getters
    ExecutionUnitId get_id() const { return unit_id; }
    const std::string& get_source_name() const { return source_name; }
    Node* get_ast() const { return ast.get(); }
    
    // Símbolos definidos por esta unidade
    const std::unordered_set<std::string>& get_defined_symbols() const {
        return defined_symbols;
    }
    
    // Símbolos usados por esta unidade
    const std::unordered_set<std::string>& get_used_symbols() const {
        return used_symbols;
    }
    
    // Adicionar símbolo definido
    void add_defined_symbol(const std::string& name) {
        defined_symbols.insert(name);
    }
    
    // Adicionar símbolo usado
    void add_used_symbol(const std::string& name) {
        used_symbols.insert(name);
    }
    
    // Verificar se a unidade está válida
    bool is_valid() const { return valid; }
    
    // Invalidar unidade
    void invalidate() { valid = false; }
    
    // Revalidar unidade
    void revalidate() { valid = true; }

private:
    ExecutionUnitId unit_id;
    std::unique_ptr<Node> ast;
    std::string source_name;
    
    std::unordered_set<std::string> defined_symbols;  // Símbolos definidos
    std::unordered_set<std::string> used_symbols;     // Símbolos usados
    
    bool valid;  // Se a unidade está válida (não invalidada)
};

} // namespace interactive
} // namespace nv
