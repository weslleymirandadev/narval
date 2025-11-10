#include "frontend/parser/parser.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

constexpr size_t MAX_LINE_LENGTH = 1024;
constexpr const char* ANSI_BOLD = "\x1b[1m";
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_RED = "\x1b[31m";

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
    std::cerr << ANSI_BOLD
              << token.filename << ":" << token.line << ":" << token.column_start << ": "
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

std::unique_ptr<Node> Parser::produce_ast(const std::vector<Token>& tokens) {
    this->tokens = tokens;
    token_count = tokens.size();
    index = 0;
    has_errors = false;
    lines.clear();
    line_count = 0;

    if (!tokens.empty()) {
        try {
            read_lines(tokens[0].filename);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not read source file: " << e.what() << "\n";
        }
    }

    auto program = std::make_unique<Program>();

    while (not_eof()) {
        Token current = current_token();
        if (current.type == TokenType::IMPORT) {
            consume_token(); // Ignora import (gerenciado pelo ModuleManager)
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