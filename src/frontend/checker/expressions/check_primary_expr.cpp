#include "frontend/checker/expressions/check_primary_expr.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/ast/expressions/identifier_node.hpp"
#include "frontend/ast/expressions/numeric_literal_node.hpp"
#include "frontend/checker/type.hpp"
#include <sstream>
#include <filesystem>
#include <unordered_set>

// Conjunto estático para rastrear erros de identificador já reportados (evitar duplicação entre checkers/ASTs clonados)
// Usa chave composta: filename:line:col:symbol
static std::unordered_set<std::string> reported_identifier_errors;

namespace {
    // Converte um caminho relativo em absoluto
    std::string to_absolute_path(const std::string& path) {
        if (path.empty()) {
            return path;
        }
        
        try {
            std::filesystem::path file_path(path);
            
            // Se já é absoluto, tentar normalizar
            if (file_path.is_absolute()) {
                try {
                    return std::filesystem::canonical(file_path).string();
                } catch (const std::filesystem::filesystem_error&) {
                    return std::filesystem::absolute(file_path).string();
                }
            }
            
            // Se é relativo, converter para absoluto
            try {
                return std::filesystem::canonical(std::filesystem::absolute(file_path)).string();
            } catch (const std::filesystem::filesystem_error&) {
                return std::filesystem::absolute(file_path).string();
            }
        } catch (const std::exception&) {
            // Se falhar, retornar o caminho original
            return path;
        }
    }
}

std::shared_ptr<nv::Type>& check_primary_expr(nv::Checker* ch, Node* node) {
    static thread_local std::shared_ptr<nv::Type> temp_result;
    switch (node->kind) {
        case NodeType::NumericLiteral: {
            const auto* vl = static_cast<NumericLiteralNode*>(node);
            if (vl->value.find('.') != std::string::npos) {
                temp_result = ch->gettyptr("float");
                return temp_result;
            }
            temp_result = ch->gettyptr("int");
            return temp_result;
        }
        case NodeType::StringLiteral:
            temp_result = ch->gettyptr("string");
            return temp_result;
        case NodeType::BooleanLiteral:
            temp_result = ch->gettyptr("bool");
            return temp_result;
        case NodeType::Identifier: {
            const auto* id = static_cast<IdentifierNode*>(node);
            // Verificar se o identificador existe antes de tentar acessá-lo
            // Usar try-catch apenas para detectar, mas converter para error() imediatamente
            try {
                auto& var_type = ch->scope->get_key(id->symbol);
                // Se for tipo polimórfico, instanciar
                if (var_type->kind == nv::Kind::POLY_TYPE) {
                    auto poly = std::static_pointer_cast<nv::PolyType>(var_type);
                    int next_id = ch->unify_ctx.get_next_var_id();
                    temp_result = poly->instantiate(next_id);
                    return temp_result;
                }
                return var_type;
            } catch (std::runtime_error&) {
                // Identificador não encontrado - reportar erro formatado (mesmo formato do parser)
                // Nunca propagar runtime_error, apenas usar error() do checker
                
                // Criar chave única baseada em conteúdo para evitar duplicação entre ASTs clonados
                // Formato: filename:line:col:symbol
                std::string error_key_str;
                if (node->position) {
                    std::ostringstream key_oss;
                    key_oss << to_absolute_path(ch->current_filename) << ":" 
                            << node->position->line << ":" 
                            << node->position->col[0] << ":" 
                            << id->symbol;
                    error_key_str = key_oss.str();
                }
                
                // Verificar se o erro já foi reportado globalmente (entre checkers/ASTs diferentes)
                if (!error_key_str.empty() && 
                    reported_identifier_errors.find(error_key_str) != reported_identifier_errors.end()) {
                    ch->err = true;  // Manter flag de erro, mas não reportar novamente
                    temp_result = ch->gettyptr("void");
                    return temp_result;
                }
                
                std::ostringstream oss;
                oss << "Identifier '" << id->symbol << "' not found.";
                ch->error(node, oss.str());
                
                // Marcar como reportado globalmente
                if (!error_key_str.empty()) {
                    reported_identifier_errors.insert(error_key_str);
                }
                
                temp_result = ch->gettyptr("void");
                return temp_result;
            }
        }
        default:
            temp_result = ch->gettyptr("void");
            return temp_result;
    }
}