#include "frontend/parser/parser.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/ast/statements/import_stmt_node.hpp"
#include "frontend/ast/statements/module_attr_node.hpp"
#include "frontend/syntax_highlighter.hpp"
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
        if (path.rfind("repl[", 0) == 0 || path.rfind("notebook[", 0) == 0 || path.rfind("repl_line_", 0) == 0 || path.rfind("cell_", 0) == 0) {
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

    std::cerr << " " << token.line << " |   "
              << nv::syntax_highlighter::highlight_line(line_content) << "\n";

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

void Parser::set_emit_diagnostics(bool enabled) {
    emit_diagnostics = enabled;
}

void Parser::error(const std::string& message) {
    Token token = current_token();
    std::string abs_filename = to_absolute_path(token.filename);
    has_errors = true;
    diagnostics.push_back({
        abs_filename,
        token.line,
        token.column_start,
        token.column_end,
        1,
        message
    });

    if (!emit_diagnostics) {
        throw std::runtime_error(message);
    }

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
        std::cerr << "ERROR: Parser index " << index << " >= token_count " << token_count << std::endl;
        std::cerr << "ERROR: Available tokens: " << token_count << std::endl;
        return Token{TokenType::UNKNOWN, "", 0, 0, 0, 0, 0, ""};  // Return error token
    }
    return tokens[index];
}

Token Parser::consume_token() {
    if (index >= token_count) {
        std::cerr << "ERROR: Parser consume_token index " << index << " >= token_count " << token_count << std::endl;
        std::cerr << "ERROR: Available tokens: " << token_count << std::endl;
        return Token{TokenType::UNKNOWN, "", 0, 0, 0, 0, 0, ""};  // Return error token
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
        oss << "Expected '" << get_token_display(expected_type) << "'"
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
    import_index = 0;
    has_errors = false;
    diagnostics.clear();
    lines.clear();
    line_count = 0;

    if (!tokens.empty()) {
        // Avoid attempting to read REPL or notebook virtual filenames
        const std::string& fname = tokens[0].filename;
        if (fname.rfind("repl[", 0) != 0 && fname.rfind("notebook[", 0) != 0 && fname.rfind("repl_line_", 0) != 0 && fname.rfind("cell_", 0) != 0) {
            // Estes são nomes virtuais que não devem ser lidos como arquivos
            try {
                read_lines(fname);
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not read source file: " << e.what() << "\n";
            }
        }
    }

    auto program = std::make_unique<Program>();

    // Atributos de módulo: [attr1, attr2, func(arg)]
    // Só são válidos no início do arquivo (antes de qualquer outro statement).
    // Lookahead: se [ for seguido somente de IDENTIFIER, COMMA, OPAREN, CPAREN → é atributo.
    // Qualquer outro token dentro (NUMBER, STRING, operador…) → vetor literal normal.
    while (not_eof() && current_token().type == TokenType::OBRACKET) {
        size_t la = index + 1;
        bool is_attr = true;
        while (la < token_count && tokens[la].type != TokenType::CBRACKET
                                && tokens[la].type != TokenType::EOF_TOKEN) {
            auto t = tokens[la].type;
            if (t != TokenType::IDENTIFIER && t != TokenType::COMMA &&
                t != TokenType::OPAREN    && t != TokenType::CPAREN) {
                is_attr = false;
                break;
            }
            ++la;
        }
        if (!is_attr || la >= token_count) break;

        auto attr_node = std::make_unique<ModuleAttrNode>();

        // Salvar posição do token '[' para o NOTE do checker
        Token open_tok = current_token();
        consume_token(); // consume [

        while (not_eof() && current_token().type != TokenType::CBRACKET) {
            if (current_token().type == TokenType::IDENTIFIER) {
                std::string attr = current_token().lexeme;
                consume_token();
                if (current_token().type == TokenType::OPAREN) {
                    consume_token();
                    if (current_token().type == TokenType::IDENTIFIER) {
                        attr += "(" + current_token().lexeme + ")";
                        consume_token();
                    }
                    expect(TokenType::CPAREN, "Expected ')' in attribute argument");
                }
                attr_node->attrs.push_back(std::move(attr));
            }
            if (current_token().type == TokenType::COMMA) consume_token();
        }

        Token close_tok = current_token();
        expect(TokenType::CBRACKET, "Expected ']' to close module attribute");

        // Posição abrange de '[' até ']' — usada pelo checker no NOTE
        attr_node->position = std::make_unique<PositionData>(
            open_tok.line,
            open_tok.column_start,
            close_tok.column_end,
            open_tok.position_start,
            close_tok.position_end,
            open_tok.filename
        );

        program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(attr_node.release())));
    }

    while (not_eof()) {
        Token current = current_token();
        try {
            std::unique_ptr<Node> stmt = parse_stmt(this);
            if (stmt) {
                program->add_statement(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
            }
        } catch (const std::exception& e) {
            if (emit_diagnostics) {
                std::cerr << "Error during statement parsing: " << e.what() << "\n";
            }
            if (has_errors) break;
        }
    }

    return program;
}
