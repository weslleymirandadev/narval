#include "frontend/checker/statements/check_match_stmt.hpp"
#include "frontend/ast/statements/match_stmt_node.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "frontend/ast/expressions/string_literal_node.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/checker/type.hpp"
#include <stdexcept>
#include <cctype>

namespace {
    // Helper: verifica se uma string é um caractere único válido para range
    // A string já vem processada do lexer (sem aspas, com escape sequences resolvidas)
    bool is_single_char_string(const std::string& str) {
        // O lexer já processou escape sequences, então a string contém o caractere real
        // Para ranges, precisamos de exatamente 1 caractere
        return str.size() == 1;
    }
    
    // Helper: extrai o caractere de uma string literal para comparação
    // A string já vem processada do lexer
    char extract_char_from_string(const std::string& str) {
        if (str.empty()) return '\0';
        return str[0]; // O lexer já processou escape sequences
    }
    
    // Helper: verifica se um range é válido (start <= end)
    bool is_valid_range(nv::Checker* checker, RangeExprNode* range, std::shared_ptr<nv::Type> target_type) {
        if (!range->start || !range->end) {
            checker->error(range, "Range expression requires both start and end");
            return false;
        }
        
        // Inferir tipos dos limites
        auto start_type = checker->infer_expr(range->start.get());
        auto end_type = checker->infer_expr(range->end.get());
        
        // Verificar compatibilidade com o tipo do target
        bool target_is_int = target_type->kind == nv::Kind::INT;
        bool target_is_string = target_type->kind == nv::Kind::STRING;
        bool start_is_int = start_type->kind == nv::Kind::INT;
        bool start_is_string = start_type->kind == nv::Kind::STRING;
        bool end_is_int = end_type->kind == nv::Kind::INT;
        bool end_is_string = end_type->kind == nv::Kind::STRING;
        
        // Verificar compatibilidade de tipos
        if (target_is_int && (!start_is_int || !end_is_int)) {
            checker->error(range, "Range bounds must be integers when matching against integer");
            return false;
        }
        
        if (target_is_string && (!start_is_string || !end_is_string)) {
            checker->error(range, "Range bounds must be strings when matching against string");
            return false;
        }
        
        // Validar ranges numéricos
        if (start_is_int && end_is_int) {
            // Ambos são numéricos - verificar valores
            if (auto* start_num = dynamic_cast<NumericLiteralNode*>(range->start.get())) {
                if (auto* end_num = dynamic_cast<NumericLiteralNode*>(range->end.get())) {
                    int start_val = std::stoi(start_num->value);
                    int end_val = std::stoi(end_num->value);
                    
                    if (start_val > end_val) {
                        checker->error(range, "Range start must be less than or equal to end");
                        return false;
                    }
                    
                    return true;
                }
            }
            // Se não são literais, assumir válido (será verificado em runtime)
            return true;
        }
        
        // Validar ranges de caracteres
        if (start_is_string && end_is_string) {
            if (auto* start_str = dynamic_cast<StringLiteralNode*>(range->start.get())) {
                if (auto* end_str = dynamic_cast<StringLiteralNode*>(range->end.get())) {
                    // Verificar se são strings de caractere único
                    if (!is_single_char_string(start_str->value)) {
                        checker->error(range->start.get(), "Range start must be a single character string");
                        return false;
                    }
                    
                    if (!is_single_char_string(end_str->value)) {
                        checker->error(range->end.get(), "Range end must be a single character string");
                        return false;
                    }
                    
                    // Extrair caracteres e comparar
                    char start_char = extract_char_from_string(start_str->value);
                    char end_char = extract_char_from_string(end_str->value);
                    
                    // Verificar ordem: start deve ser <= end (como em Rust)
                    if (start_char > end_char) {
                        checker->error(range, "Range start character must be less than or equal to end character");
                        return false;
                    }
                    
                    // Qualquer caractere é válido desde que start <= end
                    return true;
                }
            }
            // Se não são literais, assumir válido (será verificado em runtime)
            return true;
        }
        
        // Tipos incompatíveis
        checker->error(range, "Range bounds must have compatible types with match target");
        return false;
    }
}

std::shared_ptr<nv::Type>& check_match_stmt(nv::Checker* checker, Node* node) {
    auto* match_stmt = static_cast<MatchStmtNode*>(node);
    
    if (!match_stmt->target) {
        checker->error(match_stmt, "Match statement requires a target expression");
        return checker->gettyptr("void");
    }
    
    // Inferir tipo do target
    auto target_type = checker->infer_expr(match_stmt->target.get());
    target_type = checker->unify_ctx.resolve(target_type);
    
    // Verificar cada case
    for (size_t i = 0; i < match_stmt->cases.size(); i++) {
        auto& case_expr = match_stmt->cases[i];
        
        // Verificar se é um identifier padrão (default ou _)
        if (auto* id = dynamic_cast<IdentifierNode*>(case_expr.get())) {
            if (id->symbol == "default" || id->symbol == "_") {
                continue; // Default case é sempre válido
            }
        }
        
        // Verificar se é um range
        if (auto* range = dynamic_cast<RangeExprNode*>(case_expr.get())) {
            if (!is_valid_range(checker, range, target_type)) {
                // Erro já foi reportado
                continue;
            }
        }
        
        // Para outros tipos de expressões, verificar compatibilidade básica
        // (será verificado em runtime durante a comparação)
    }
    
    return checker->gettyptr("void");
}
