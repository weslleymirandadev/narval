#include "frontend/lexer/number_tokenizer.hpp"

namespace {
bool is_base_digit(char c, int base) {
    switch (base) {
        case 2:
            return c == '0' || c == '1';
        case 8:
            return c >= '0' && c <= '7';
        case 10:
            return std::isdigit(static_cast<unsigned char>(c));
        case 16:
            return std::isxdigit(static_cast<unsigned char>(c));
        default:
            return false;
    }
}

void consume_digits_with_separators(
    const std::string& input,
    size_t& pos,
    size_t& column,
    std::string& value,
    int base,
    size_t line,
    size_t start_column,
    const std::string& context,
    bool require_digit = true
) {
    bool saw_digit = false;
    bool previous_was_separator = false;

    while (pos < input.size()) {
        char current = input[pos];

        if (is_base_digit(current, base)) {
            value += current;
            ++pos;
            ++column;
            saw_digit = true;
            previous_was_separator = false;
            continue;
        }

        if (current == '_') {
            if (!saw_digit || previous_was_separator ||
                pos + 1 >= input.size() || !is_base_digit(input[pos + 1], base)) {
                throw std::runtime_error(
                    "Invalid numeric separator in " + context + " at line " +
                    std::to_string(line) + ", column " + std::to_string(start_column));
            }

            ++pos;
            ++column;
            previous_was_separator = true;
            continue;
        }

        break;
    }

    if (require_digit && !saw_digit) {
        throw std::runtime_error(
            "Invalid number format: missing digits in " + context + " at line " +
            std::to_string(line) + ", column " + std::to_string(start_column));
    }

    if (previous_was_separator) {
        throw std::runtime_error(
            "Invalid numeric separator in " + context + " at line " +
            std::to_string(line) + ", column " + std::to_string(start_column));
    }
}
}

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
                consume_digits_with_separators(input, pos, column, value, 2, line, start_column, "binary literal");
            } else if (next == 'o') {
                consume_digits_with_separators(input, pos, column, value, 8, line, start_column, "octal literal");
            } else if (next == 'x') {
                consume_digits_with_separators(input, pos, column, value, 16, line, start_column, "hex literal");
            }
            return Token(TokenType::NUMBER, value, line, start_column, column, start_position, pos, filename);
        }
    }

    // verifica float
    consume_digits_with_separators(input, pos, column, value, 10, line, start_column, "decimal literal");

    if (pos < input.size() && input[pos] == '_' && pos + 1 < input.size() &&
        (input[pos + 1] == '.' || input[pos + 1] == 'e' || input[pos + 1] == 'E')) {
        throw std::runtime_error("Invalid numeric separator in decimal literal at line " + std::to_string(line) + ", column " + std::to_string(start_column));
    }

    if (pos < input.size() && input[pos] == '.' && (pos + 1 >= input.size() || input[pos + 1] != '.')) {
        is_float = true;
        value += input[pos];
        ++pos;
        ++column;

        if (pos < input.size() && input[pos] == '_') {
            throw std::runtime_error("Invalid numeric separator in float literal at line " + std::to_string(line) + ", column " + std::to_string(start_column));
        }

        consume_digits_with_separators(input, pos, column, value, 10, line, start_column, "float literal", false);
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
        consume_digits_with_separators(input, pos, column, value, 10, line, start_column, "scientific notation exponent");
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
