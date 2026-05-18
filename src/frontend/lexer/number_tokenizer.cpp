#include "frontend/lexer/number_tokenizer.hpp"

#include <algorithm>

namespace {
[[noreturn]] void throw_number_error(
    const std::string& filename,
    size_t line,
    size_t col_start,
    size_t col_end,
    const std::string& message
) {
    throw LexicalError(filename, line, col_start, col_end, message);
}

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
    const std::string& filename,
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
                throw_number_error(
                    filename,
                    line,
                    column,
                    column + 1,
                    "Invalid numeric separator in " + context);
            }

            ++pos;
            ++column;
            previous_was_separator = true;
            continue;
        }

        break;
    }

    if (require_digit && !saw_digit) {
        throw_number_error(
            filename,
            line,
            start_column,
            std::max(start_column + 1, column),
            "Invalid number format: missing digits in " + context);
    }

    if (previous_was_separator) {
        size_t separator_column = column > 1 ? column - 1 : column;
        throw_number_error(
            filename,
            line,
            separator_column,
            separator_column + 1,
            "Invalid numeric separator in " + context);
    }
}
}

Token tokenize_number(const std::string& input, size_t& pos, size_t& line, size_t& column, const std::string& filename) {
    std::string value;
    size_t start_column = column;
    size_t start_position = pos;
    bool is_float = false;
    bool has_exponent = false;

    if (input[pos] == '-') {
        value += input[pos];
        ++pos;
        ++column;
    }

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
                consume_digits_with_separators(input, pos, column, value, 2, line, start_column, filename, "binary literal");
            } else if (next == 'o') {
                consume_digits_with_separators(input, pos, column, value, 8, line, start_column, filename, "octal literal");
            } else if (next == 'x') {
                consume_digits_with_separators(input, pos, column, value, 16, line, start_column, filename, "hex literal");
            }
            return Token(TokenType::NUMBER, value, line, start_column, column, start_position, pos, filename);
        }
    }

    consume_digits_with_separators(input, pos, column, value, 10, line, start_column, filename, "decimal literal");

    if (pos < input.size() && input[pos] == '_' && pos + 1 < input.size() &&
        (input[pos + 1] == '.' || input[pos + 1] == 'e' || input[pos + 1] == 'E')) {
        throw_number_error(filename, line, column, column + 1, "Invalid numeric separator in decimal literal");
    }

    if (pos < input.size() && input[pos] == '.' && (pos + 1 >= input.size() || input[pos + 1] != '.')) {
        is_float = true;
        value += input[pos];
        ++pos;
        ++column;

        if (pos < input.size() && input[pos] == '_') {
            throw_number_error(filename, line, column, column + 1, "Invalid numeric separator in float literal");
        }

        consume_digits_with_separators(input, pos, column, value, 10, line, start_column, filename, "float literal", false);
    }

    if (pos < input.size() && (input[pos] == 'e' || input[pos] == 'E')) {
        has_exponent = true;
        value += input[pos];
        ++pos;
        ++column;
        if (pos < input.size() && (input[pos] == '-' || input[pos] == '+')) {
            value += input[pos];
            ++pos;
            ++column;
        }
        if (pos >= input.size() || !std::isdigit(static_cast<unsigned char>(input[pos]))) {
            throw_number_error(filename, line, column, column + 1, "Invalid scientific notation: missing exponent");
        }
        consume_digits_with_separators(input, pos, column, value, 10, line, start_column, filename, "scientific notation exponent");
        if (is_float && (value.back() == '.' || value == "-.") ) {
            throw_number_error(filename, line, start_column, column, "Invalid number format");
        }
        if (has_exponent && (value.back() == 'e' || value.back() == 'E')) {
            throw_number_error(filename, line, column, column + 1, "Invalid scientific notation: missing exponent");
        }
        return Token(TokenType::NUMBER, value, line, start_column, column, start_position, pos, filename);
    }

    return Token(TokenType::NUMBER, value, line, start_column, column, start_position, pos, filename);
}
