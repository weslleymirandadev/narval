#include "frontend/lexer/string_tokenizer.hpp"
#include <stdexcept>

Token tokenize_string(const std::string& input, size_t& position, size_t line, size_t column, const std::string& filename) {
    size_t start_pos = position;
    size_t start_col = column;
    char quote = input[position]; // ' or "

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
                case '\\': value += '\\'; break;
                case '\'': value += '\''; break;
                case '"': value += '"'; break;
                default: value += '\\'; value += c; // keep unknown
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == quote) {
            // End of string
            ++position;
            ++column;
            return Token(TokenType::STRING, value, line, start_col, column, start_pos, position, filename);
        } else if (c == '\n') {
            throw std::runtime_error("Line break not allowed inside string literal");
        } else {
            value += c;
        }

        ++position;
        ++column;
    }

    throw std::runtime_error("String literal not closed");
}