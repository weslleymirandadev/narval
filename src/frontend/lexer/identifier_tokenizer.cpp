#include "frontend/lexer/identifier_tokenizer.hpp"
#include <cctype>

Token tokenize_identifier_or_keyword(const std::string& input, size_t& pos, size_t& line, size_t& column, const std::string& filename) {
    std::string value;
    size_t start_column = column;
    size_t start_position = pos;

    while (pos < input.size() && (std::isalnum(input[pos]) || input[pos] == '_')) {
        value += input[pos];
        ++pos;
        ++column;
    }

    TokenType type = TokenType::IDENTIFIER;
    
         if (value == "if") type = TokenType::IF;
    else if (value == "elif") type = TokenType::ELIF;
    else if (value == "else") type = TokenType::ELSE;
    else if (value == "match") type = TokenType::MATCH;
    else if (value == "for") type = TokenType::FOR;
    else if (value == "while") type = TokenType::WHILE;
    else if (value == "loop") type = TokenType::LOOP;
    else if (value == "break") type = TokenType::BREAK;
    else if (value == "continue") type = TokenType::CONTINUE;
    else if (value == "lock") type = TokenType::LOCK;
    else if (value == "label") type = TokenType::LABEL;
    else if (value == "return") type = TokenType::RETURN;
    else if (value == "true") type = TokenType::TRUE;
    else if (value == "false") type = TokenType::FALSE;
    else if (value == "_") type = TokenType::UNDERSCORE;

    return Token(type, value, line, start_column, column, start_position, pos, filename);
}
