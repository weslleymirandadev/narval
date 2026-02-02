#pragma once

#include "frontend/checker/namespace.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/ast/ast.hpp"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace nv {
namespace interactive {

/**
 * ID único para unidades de execução (REPL linha ou célula de notebook)
 */
using ExecutionUnitId = uint64_t;

/**
 * Informações sobre a origem de um símbolo
 */
struct SymbolOrigin {
    ExecutionUnitId unit_id;  // ID da unidade que definiu o símbolo
    std::string source_name;   // Nome da origem (ex: "repl", "cell_1")
    
    SymbolOrigin(ExecutionUnitId id, const std::string& name)
        : unit_id(id), source_name(name) {}
};

/**
 * Informações sobre um símbolo na sessão
 */
struct SessionSymbol {
    std::shared_ptr<Type> type;           // Tipo do símbolo
    SymbolOrigin origin;                  // Origem do símbolo
    bool is_valid;                        // Se o símbolo está válido (não invalidado)
    std::unordered_set<ExecutionUnitId> dependents;  // Unidades que dependem deste símbolo
    
    // Construtor padrão necessário para uso com unordered_map::operator[]
    SessionSymbol() : type(nullptr), origin(0, ""), is_valid(false) {}
    
    SessionSymbol(std::shared_ptr<Type> t, const SymbolOrigin& orig)
        : type(t), origin(orig), is_valid(true) {}
};

/**
 * Session Manager - Componente central do modo interativo
 * 
 * Responsabilidades:
 * - Manter Symbol Table viva entre execuções
 * - Associar símbolos a unidades de origem
 * - Controlar ciclo de vida lógico dos valores
 * - Detectar redefinições de símbolos
 * - Informar dependências entre símbolos
 */
class SessionManager {
public:
    SessionManager();
    ~SessionManager() = default;
    
    // Gerenciamento de símbolos
    /**
     * Define um novo símbolo na sessão
     * @param name Nome do símbolo
     * @param type Tipo do símbolo
     * @param unit_id ID da unidade que define o símbolo
     * @param source_name Nome da origem (ex: "repl", "cell_1")
     * @return true se definido com sucesso, false se já existe (redefinição)
     */
    bool define_symbol(const std::string& name, 
                       std::shared_ptr<Type> type,
                       ExecutionUnitId unit_id,
                       const std::string& source_name);
    
    /**
     * Atualiza um símbolo existente (redefinição)
     * @param name Nome do símbolo
     * @param type Novo tipo do símbolo
     * @param unit_id ID da unidade que redefine
     * @param source_name Nome da origem
     * @return true se atualizado, false se não existe
     */
    bool update_symbol(const std::string& name,
                      std::shared_ptr<Type> type,
                      ExecutionUnitId unit_id,
                      const std::string& source_name);
    
    /**
     * Busca um símbolo na sessão
     * @param name Nome do símbolo
     * @return Ponteiro para SessionSymbol ou nullptr se não encontrado
     */
    SessionSymbol* lookup_symbol(const std::string& name);
    
    /**
     * Verifica se um símbolo existe e está válido
     */
    bool has_symbol(const std::string& name) const;
    
    /**
     * Registra uma dependência: unit_id depende do símbolo 'name'
     */
    void add_dependency(ExecutionUnitId unit_id, const std::string& symbol_name);
    
    /**
     * Invalida um símbolo e todos os seus dependentes
     * @param symbol_name Nome do símbolo a invalidar
     * @return Set de unit_ids que foram invalidados
     */
    std::unordered_set<ExecutionUnitId> invalidate_symbol(const std::string& symbol_name);
    
    /**
     * Invalida todos os símbolos de uma unidade específica
     * @param unit_id ID da unidade
     * @return Set de unit_ids que foram invalidados
     */
    std::unordered_set<ExecutionUnitId> invalidate_unit(ExecutionUnitId unit_id);
    
    /**
     * Obtém todos os símbolos definidos por uma unidade
     */
    std::vector<std::string> get_symbols_by_unit(ExecutionUnitId unit_id) const;
    
    /**
     * Obtém todas as dependências de uma unidade
     */
    std::unordered_set<std::string> get_unit_dependencies(ExecutionUnitId unit_id) const;
    
    /**
     * Cria um novo ID único para uma unidade de execução
     */
    ExecutionUnitId create_unit_id();
    
    /**
     * Obtém o Namespace global da sessão (para uso pelo checker incremental)
     */
    std::shared_ptr<Namespace> get_global_namespace() const { return global_namespace; }
    
    /**
     * Limpa toda a sessão (reset completo)
     */
    void clear();
    
    /**
     * Obtém estatísticas da sessão (para debug)
     */
    struct SessionStats {
        size_t total_symbols;
        size_t valid_symbols;
        size_t total_units;
    };
    SessionStats get_stats() const;

private:
    // Tabela de símbolos da sessão
    std::unordered_map<std::string, SessionSymbol> session_symbols;
    
    // Mapeamento: unit_id -> símbolos definidos por essa unidade
    std::unordered_map<ExecutionUnitId, std::unordered_set<std::string>> unit_symbols;
    
    // Mapeamento: unit_id -> símbolos usados por essa unidade
    std::unordered_map<ExecutionUnitId, std::unordered_set<std::string>> unit_dependencies;
    
    // Namespace global compartilhado (para integração com checker)
    std::shared_ptr<Namespace> global_namespace;
    
    // Contador para gerar IDs únicos
    ExecutionUnitId next_unit_id;
    
    /**
     * Propaga invalidação recursivamente através das dependências
     */
    void propagate_invalidation(const std::string& symbol_name, 
                               std::unordered_set<ExecutionUnitId>& invalidated);
};

} // namespace interactive
} // namespace nv
