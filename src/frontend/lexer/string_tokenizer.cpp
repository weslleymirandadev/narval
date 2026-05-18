#include "frontend/lexer/string_tokenizer.hpp"

Token tokenize_string(const std::string& input, size_t& position, size_t& line, size_t& column, const std::string& filename) {
    size_t start_pos = position;
    size_t start_line = line;
    size_t start_col = column;
    char quote = input[position]; // ' or " or `
    bool is_backtick_string = quote == '`';
    bool is_char_literal = quote == '\'';

    // Advances the initial quote
    ++position;
    ++column;

    std::string value;
    bool escaped = false;

    while (position < input.size()) {
        char c = input[position];

        if (escaped) {
            switch (c) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case '0': value += '\0'; break;
                case '\\': value += '\\'; break;
                case '\'': value += '\''; break;
                case '"': value += '"'; break;
                case '`': value += '`'; break;
                default: value += '\\'; value += c; // keep unknown
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == quote) {
            // End of string
            ++position;
            ++column;
            if (is_char_literal) {
                if (value.size() != 1) {
                    throw LexicalError(
                        filename,
                        start_line,
                        start_col,
                        column,
                        "Char literal must contain exactly one character");
                }
            }
            return Token(TokenType::STRING, value, start_line, start_col, column, start_pos, position, filename, quote);
        } else if (c == '\n') {
            if (!is_backtick_string) {
                throw LexicalError(
                    filename,
                    line,
                    column,
                    column + 1,
                    "Line break not allowed inside string literal");
            }

            value += c;
            ++position;
            ++line;
            column = 1;
            continue;
        } else {
            value += c;
        }

        ++position;
        ++column;
    }

    throw LexicalError(
        filename,
        start_line,
        start_col,
        start_col + 1,
        "String literal not closed");
}
