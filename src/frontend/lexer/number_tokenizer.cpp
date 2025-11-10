#include "frontend/lexer/number_tokenizer.hpp"

Token tokenize_number(const std::string& input, size_t& pos, size_t& line, size_t& column, const std::string& filename) {
    std::string value;
    size_t start_column = column;
    size_t start_position = pos;
    bool is_float = false;
    bool has_exponent = false;

    // verifica sinal
    if (input[pos] == '-') {
        value += input[pos];
        ++pos;
        ++column;
    }

    // verifica base (0b, 0o, 0x)
    if (pos + 1 < input.size() && input[pos] == '0') {
        char next = input[pos + 1];
        if (next == 'b' || next == 'o' || next == 'x') {
            value += input[pos];
            ++pos;
            ++column;
            value += input[pos];
            ++pos;
            ++column;
            if (next == 'b') {
                while (pos < input.size() && (input[pos] == '0' || input[pos] == '1')) {
                    value += input[pos];
                    ++pos;
                    ++column;
                }
            } else if (next == 'o') {
                while (pos < input.size() && input[pos] >= '0' && input[pos] <= '7') {
                    value += input[pos];
                    ++pos;
                    ++column;
                }
            } else if (next == 'x') {
                while (pos < input.size() && std::isxdigit(input[pos])) {
                    value += input[pos];
                    ++pos;
                    ++column;
                }
            }
            return Token(TokenType::NUMBER, value, line, start_column, column, start_position, pos, filename);
        }
    }

    // verifica float
    while (pos < input.size() && (std::isdigit(input[pos]) || (input[pos] == '.' && input[pos + 1] != '.'))) {
        if (input[pos] == '.') {
            if (is_float) break;
            is_float = true;
        }
        value += input[pos];
        ++pos;
        ++column;
    }
    // notação científica (e8, e-9, E+10)
    if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
        has_exponent = true;
        value += input[pos];
        ++pos;
        ++column;
        // sinal do expoente (opcional)
        if (pos < input.size() && (input[pos] == '-' || input[pos] == '+')) {
            value += input[pos];
            ++pos;
            ++column;
        }
        // dígitos do expoente (obrigatório)
        if (pos >= input.size() || !std::isdigit(input[pos])) {
            throw std::runtime_error("Invalid scientific notation: missing exponent at line " + std::to_string(line) + ", column " + std::to_string(start_column));
        }
        while (pos < input.size() && std::isdigit(input[pos])) {
            value += input[pos];
            ++pos;
            ++column;
        }
        // formato inválido
        if (is_float && (value.back() == '.' || value == "-.") ) {
            throw std::runtime_error("Invalid number format at line " + std::to_string(line) + ", column " + std::to_string(start_column));
        }
        if (has_exponent && (value.back() == 'e' || value.back() == 'E')) {
            throw std::runtime_error("Invalid scientific notation: missing exponent at line " + std::to_string(line) + ", column " + std::to_string(start_column));
        }
        return Token(TokenType::NUMBER, value, line, start_column, column, start_position, pos, filename);
    }

    return Token(TokenType::NUMBER, value, line, start_column, column, start_position, pos, filename);
}
