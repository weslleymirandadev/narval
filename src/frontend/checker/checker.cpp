#include "frontend/checker/checker.hpp"
#include "frontend/checker/type.hpp"
#include "frontend/checker/unification.hpp"
#include "frontend/checker/builtins.hpp"
#include "frontend/checker/expressions/check_call_expr.hpp"
#include "frontend/checker/expressions/check_primary_expr.hpp"
#include "frontend/checker/expressions/check_binary_expr.hpp"
#include "frontend/checker/expressions/check_assignment_expr.hpp"
#include "frontend/checker/expressions/check_array_expr.hpp"
#include "frontend/checker/expressions/check_tuple_expr.hpp"
#include "frontend/checker/expressions/check_vector_expr.hpp"
#include "frontend/checker/checker_meth.hpp"
#include "frontend/ast/ast.hpp"
#include <memory>
#include <unordered_set>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <sstream>

constexpr size_t MAX_LINE_LENGTH = 1024;
constexpr const char* ANSI_BOLD = "\x1b[1m";
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_RED = "\x1b[31m";

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

nv::Checker::Checker() {
    err = false;  // Inicializar flag de erro
    reported_errors.clear();  // Inicializar conjunto de erros reportados
    auto globalnamespace = std::make_shared<Namespace>();
    namespaces.push_back(globalnamespace);
    scope = globalnamespace;
    types["int"] = std::make_shared<nv::Int>();
    types["string"] = std::make_shared<nv::String>();
    types["float"] = std::make_shared<nv::Float>();
    types["bool"] = std::make_shared<nv::Boolean>();
    types["void"] = std::make_shared<nv::Void>();
    
    // Registrar funções builtin do runtime
    register_builtins(*this);
}

nv::Type& nv::Checker::getty(std::string ty) {
    return *types.at(ty);
}

std::shared_ptr<nv::Type>& nv::Checker::gettyptr(std::string ty){
    // Verificar se já existe no cache
    auto it = types.find(ty);
    if (it != types.end()) {
        return it->second;
    }
    
    // Parsear tipos compostos: vector, int[10], string[5], etc.
    
    // Tipo vector
    if (ty == "vector") {
        auto vec_type = std::make_shared<nv::Vector>();
        types[ty] = vec_type;
        return types[ty];
    }
    
    // Tipo array: int[10], string[5], etc.
    // Formato: base_type[size]
    size_t bracket_pos = ty.find('[');
    if (bracket_pos != std::string::npos && bracket_pos > 0) {
        std::string base_type_str = ty.substr(0, bracket_pos);
        size_t close_bracket = ty.find(']', bracket_pos);
        if (close_bracket != std::string::npos) {
            std::string size_str = ty.substr(bracket_pos + 1, close_bracket - bracket_pos - 1);
            try {
                int size = std::stoi(size_str);
                if (size > 0) {
                    // Obter tipo base
                    auto& base_type = gettyptr(base_type_str);
                    auto arr_type = std::make_shared<nv::Array>(base_type, size);
                    types[ty] = arr_type;
                    return types[ty];
                }
            } catch (...) {
                // Não é um número válido
            }
        }
    }
    
    // Tipo não encontrado
    // Não temos Node aqui, então usar erro genérico
    std::string abs_filename = to_absolute_path(current_filename);
    std::cerr << ANSI_BOLD << abs_filename << ": "
              << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
              << "Unknown type: " << ty << ANSI_RESET << "\n\n";
    err = true;
    // Retornar void como fallback
    return types["void"];
}
void nv::Checker::push_scope() {
    auto ns = std::make_shared<Namespace>(scope);
    namespaces.push_back(ns);
    scope = ns;
}

void nv::Checker::pop_scope() {
    namespaces.pop_back();
    scope = namespaces[namespaces.size() - 1];
}

std::unordered_set<int> nv::Checker::get_free_vars_in_env() {
    std::unordered_set<int> free_vars;
    
    // Coletar variáveis livres de todas as variáveis no ambiente atual
    // O método collect_free_vars do Namespace já coleta recursivamente
    // de todos os escopos pais através da cadeia de parent, então
    // precisamos apenas chamar no escopo atual
    if (scope) {
        scope->collect_free_vars(free_vars);
    }
    
    return free_vars;
}

void nv::Checker::read_lines(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        // Se não conseguir abrir, apenas limpar linhas (erro será reportado sem contexto)
        lines.clear();
        line_count = 0;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.size() > MAX_LINE_LENGTH) {
            // Linha muito longa, truncar
            line = line.substr(0, MAX_LINE_LENGTH);
        }
        lines.push_back(line);
    }
    line_count = lines.size();
}

void nv::Checker::print_error_context(const PositionData* pos) {
    if (!pos || lines.empty() || pos->line == 0 || pos->line - 1 >= line_count) {
        return;
    }

    std::string line_content = lines[pos->line - 1];
    std::replace(line_content.begin(), line_content.end(), '\n', ' ');

    std::cerr << " " << pos->line << " |   " << line_content << "\n";

    int line_width = pos->line > 0 ? static_cast<int>(std::log10(pos->line) + 1) : 1;
    std::cerr << std::string(line_width, ' ') << "  |";
    std::cerr << std::string(pos->col[0] - 1 + 3, ' ');

    std::cerr << ANSI_RED;
    for (size_t i = pos->col[0]; i < pos->col[1]; ++i) {
        std::cerr << "^";
    }
    std::cerr << ANSI_RESET << "\n\n";
}

void nv::Checker::set_source_file(const std::string& filename) {
    current_filename = filename;
    read_lines(filename);
}

void nv::Checker::error(Node* node, const std::string& message) {
    // Evitar reportar o mesmo erro duas vezes usando o ponteiro do nó
    // O ponteiro do nó é único e não muda, então é a forma mais confiável de identificar o mesmo erro
    if (node && reported_errors.find(reinterpret_cast<const void*>(node)) != reported_errors.end()) {
        err = true;  // Manter flag de erro, mas não reportar novamente
        return;
    }
    
    std::string abs_filename = to_absolute_path(current_filename);
    
    // Marcar este nó como tendo tido erro reportado ANTES de reportar
    // para evitar que seja reportado novamente em chamadas recursivas
    if (node) {
        reported_errors.insert(reinterpret_cast<const void*>(node));
    }
    
    if (!node || !node->position) {
        std::cerr << ANSI_BOLD << abs_filename << ": "
                  << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
                  << message << ANSI_RESET << "\n\n";
        err = true;
        std::cerr.flush();
        return;
    }
    
    PositionData* pos = node->position.get();
    std::cerr << ANSI_BOLD
              << abs_filename << ":" << pos->line << ":" << pos->col[0] << ": "
              << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
              << message << ANSI_RESET << "\n";

    print_error_context(pos);
    err = true;
    std::cerr.flush();  // Garantir que a mensagem foi exibida antes de continuar
}

std::shared_ptr<nv::Type> nv::Checker::infer_type(Node* node) {
    return infer_expr(node);
}

std::shared_ptr<nv::Type> nv::Checker::infer_expr(Node* node) {
    // Não parar mesmo se houver erros anteriores - continuar verificando
    // para reportar todos os erros possíveis
    // O método error() já previne duplicação usando reported_errors
    
    switch (node->kind) {
        case NodeType::NumericLiteral:
        case NodeType::StringLiteral:
        case NodeType::BooleanLiteral:
        case NodeType::Identifier: {
            auto& result = check_primary_expr(this, node);
            return result;
        }
        
        case NodeType::BinaryExpression:
            return check_binary_expr(this, node);
        
        case NodeType::CallExpression: {
            // Delegar para check_call_expr para evitar duplicação de lógica e erros duplicados
            // check_call_expr já faz toda a verificação necessária e reporta erros corretamente
            auto& result = check_call_expr(this, node);
            return result;
        }
        
        case NodeType::ArrayExpression:
            return check_array_expr(this, node);
        
        case NodeType::VectorExpression:
            return check_vector_expr(this, node);
        
        case NodeType::TupleExpression:
            return check_tuple_expr(this, node);
        
        case NodeType::AssignmentExpression:
            return check_assignment_expr(this, node);
        
        default:
            // Para outros tipos, usar verificação tradicional
            return check_node(node);
    }
}