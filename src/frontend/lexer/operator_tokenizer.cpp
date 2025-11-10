#include "frontend/lexer/operator_tokenizer.hpp"
#include "frontend/lexer/lexer.hpp"

Token tokenize_operator(const std::string& input, size_t& pos, size_t& line, size_t& column, const std::string& filename) {
    std::string value;
    size_t start_column = column;
    size_t start_position = pos;

    if (pos + 3 <= input.size()) {
        std::string candidate = input.substr(pos, 3);
        if (candidate == "...") {
            value = candidate;
            pos += 3;
            column += 3;
            return Token(TokenType::ELIPSIS, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "..=") {
            value = candidate;
            pos += 3;
            column += 3;
            return Token(TokenType::INCLUSIVE_RANGE, value, line, start_column, column, start_position, pos, filename);
        }
    }

    if (pos + 2 <= input.size()) {
        std::string candidate = input.substr(pos, 2);
        if (candidate == "==") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::EQUALS, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "**") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::POWER, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "&&") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::AND, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "++") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::INCREMENT, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "--") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::DECREMENT, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "//") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::INTEGER_DIV, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "||") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::OR, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "!=") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::DIFFERENT, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "<=") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::LESS_THAN_EQUALS, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == ">=") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::GREATER_THAN_EQUALS, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "=>") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::ARROW, value, line, start_column, column, start_position, pos, filename);
        } else if (candidate == "..") {
            value = candidate;
            pos += 2;
            column += 2;
            return Token(TokenType::RANGE, value, line, start_column, column, start_position, pos, filename);
        }
    }

    char c = input[pos];
    if (c == '=') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::ASSIGNMENT, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '+') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::PLUS, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '-') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::MINUS, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '*') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::MUL, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '/') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::DIV, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '%') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::MOD, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '!') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::NOT, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '<') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::LT, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '>') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::GT, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '.') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::DOT, value, line, start_column, column, start_position, pos, filename);
    }else if (c == ',') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::COMMA, value, line, start_column, column, start_position, pos, filename);
    } else if (c == ';') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::SEMICOLON, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '(') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::OPAREN, value, line, start_column, column, start_position, pos, filename);
    } else if (c == ')') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::CPAREN, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '[') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::OBRACKET, value, line, start_column, column, start_position, pos, filename);
    } else if (c == ']') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::CBRACKET, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '{') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::OBRACE, value, line, start_column, column, start_position, pos, filename);
    } else if (c == '}') {
        value = std::string(1, c);
        pos += 1;
        column += 1;
        return Token(TokenType::CBRACE, value, line, start_column, column, start_position, pos, filename);
    } else if (c == ':') {
        value = std::string(1, c);
        pos += 1;
        column += 1; //voltaste
        return Token(TokenType::COLON, value, line, start_column, column, start_position, pos, filename);
    } else {
        throw std::runtime_error("Invalid operator at line " + std::to_string(line) + ", column " + std::to_string(start_column));
    }
}