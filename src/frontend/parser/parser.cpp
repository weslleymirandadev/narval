#include "frontend/parser/parser.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/ast/statements/import_stmt_node.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>

constexpr size_t MAX_LINE_LENGTH = 1024;
constexpr const char* ANSI_BOLD = "\x1b[1m";
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_RED = "\x1b[31m";

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

void Parser::read_lines(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Error opening file: " + filename);
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.size() > MAX_LINE_LENGTH) {
            throw std::runtime_error("Line exceeds maximum length");
        }
        lines.push_back(line);
    }
    line_count = lines.size();
}

void Parser::print_error_context(const Token& token) {
    if (lines.empty() || token.line - 1 >= line_count) {
        return;
    }

    std::string line_content = lines[token.line - 1];
    std::replace(line_content.begin(), line_content.end(), '\n', ' ');

    std::cerr << " " << token.line << " |   " << line_content << "\n";

    int line_width = token.line > 0 ? static_cast<int>(std::log10(token.line) + 1) : 1;
    std::cerr << std::string(line_width, ' ') << "  |";
    std::cerr << std::string(token.column_start - 1 + 3, ' ');

    std::cerr << ANSI_RED;
    for (size_t i = token.column_start; i < token.column_end; ++i) {
        std::cerr << "^";
    }
    std::cerr << ANSI_RESET << "\n\n";
}

bool Parser::has_error() const {
    return has_errors;
}

void Parser::error(const std::string& message) {
    Token token = current_token();
    std::string abs_filename = to_absolute_path(token.filename);
    std::cerr << ANSI_BOLD
              << abs_filename << ":" << token.line << ":" << token.column_start << ": "
              << ANSI_RED << "ERROR" << ANSI_RESET << ANSI_BOLD << ": "
              << message << ANSI_RESET << "\n";

    print_error_context(token);
    exit(1);
}

size_t Parser::get_token_count() const {
    return token_count;
}

bool Parser::not_eof() const {
    return index < token_count && tokens[index].type != TokenType::EOF_TOKEN;
}

Token Parser::current_token() const {
    if (index >= token_count) {
        throw std::out_of_range("Parser index out of bounds");
    }
    return tokens[index];
}

Token Parser::consume_token() {
    if (index >= token_count) {
        throw std::out_of_range("Parser index out of bounds");
    }
    return tokens[index++];
}

Token Parser::next_token() const {
    if (index + 1 >= token_count) {
        return current_token();
    }
    return tokens[index + 1];
}

Token Parser::expect(TokenType expected_type, const std::string& error_msg) {
    Token prev = consume_token();

    if (prev.type == TokenType::EOF_TOKEN) {
        error(error_msg);
        error("Reached end of file");
        throw std::runtime_error("Unexpected end of file");
    }

    if (prev.type != expected_type) {
        std::ostringstream oss;
        oss << "Expected token type " << get_token_name(expected_type)
            << ", but got token: '" << prev.lexeme << "'.";
        --index;
        error(oss.str());
    }

    return prev;
}

std::unique_ptr<Node> Parser::produce_ast(const std::vector<Token>& tokens, const std::vector<ImportInfo>& imports) {
    this->tokens = tokens;
    this->import_infos = imports;
    token_count = tokens.size();
    index = 0;
    has_errors = false;
    lines.clear();
    line_count = 0;
    size_t import_index = 0;

    if (!tokens.empty()) {
        // Avoid attempting to read REPL or notebook virtual filenames
        const std::string& fname = tokens[0].filename;
        if (fname.rfind("repl_line_", 0) != 0 && fname.rfind("cell_", 0) != 0) {
            try {
                read_lines(fname);
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not read source file: " << e.what() << "\n";
            }
        }
    }

    auto program = std::make_unique<Program>();

    while (not_eof()) {
        Token current = current_token();
        if (current.type == TokenType::IMPORT) {
            // Cria nó ImportStatement usando as informações do lexer
            if (import_index < import_infos.size()) {
                const auto& import_info = import_infos[import_index];
                std::vector<ImportItem> items;
                // Usar import_items se disponível (com posições), senão usar imports (compatibilidade)
                if (!import_info.import_items.empty()) {
                    for (const auto& item_info : import_info.import_items) {
                        items.emplace_back(item_info.name, item_info.alias, item_info.line, item_info.col_start, item_info.col_end);
                    }
                } else {
                    for (const auto& [name, alias] : import_info.imports) {
                        items.emplace_back(name, alias);
                    }
                }
                
                // Procurar o token STRING correspondente ao module_path
                // O token STRING vem antes do token IMPORT na lista de tokens
                Token string_token = current;
                // Procurar para trás pelo token STRING que corresponde ao module_path
                for (int i = static_cast<int>(index) - 1; i >= 0; --i) {
                    if (tokens[i].type == TokenType::STRING && 
                        tokens[i].lexeme == import_info.module_path &&
                        tokens[i].filename == current.filename) {
                        string_token = tokens[i];
                        break;
                    }
                }
                
                auto import_stmt = std::make_unique<ImportStmtNode>(import_info.module_path, items, current.filename);
                // Usar a posição do token STRING (que contém o caminho do módulo)
                import_stmt->position = std::make_unique<PositionData>(
                    string_token.line, string_token.column_start, string_token.column_end,
                    string_token.position_start, string_token.position_end
                );
                program->add_statement(std::move(import_stmt));
                import_index++;
            }
            consume_token();
            continue;
        }
        try {
            std::unique_ptr<Node> stmt = parse_stmt(this);
            if (stmt) {
                program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
            }
        } catch (const std::exception& e) {
            std::cerr << "Error during statement parsing: " << e.what() << "\n";
            if (has_errors) break;
        }
    }

    return program;
}