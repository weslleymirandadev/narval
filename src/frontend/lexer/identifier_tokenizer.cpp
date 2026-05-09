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
    else if (value == "forever") type = TokenType::FOREVER;
    else if (value == "break") type = TokenType::BREAK;
    else if (value == "continue") type = TokenType::CONTINUE;
    else if (value == "mut") type = TokenType::MUT;
    else if (value == "def") type = TokenType::DEF;
    else if (value == "return") type = TokenType::RETURN;
    else if (value == "true") type = TokenType::TRUE;
    else if (value == "false") type = TokenType::FALSE;
    else if (value == "_") type = TokenType::UNDERSCORE;
    else if (value == "class") type = TokenType::CLASS;
    else if (value == "public") type = TokenType::PUBLIC;
    else if (value == "private") type = TokenType::PRIVATE;
    else if (value == "protected") type = TokenType::PROTECTED;
    else if (value == "abstract") type = TokenType::ABSTRACT;
    else if (value == "extends") type = TokenType::EXTENDS;
    else if (value == "implements") type = TokenType::IMPLEMENTS;
    else if (value == "interface") type = TokenType::INTERFACE;
    else if (value == "new") type = TokenType::NEW;
    else if (value == "self") type = TokenType::SELF;
    else if (value == "super") type = TokenType::SUPER;
    else if (value == "instanceof") type = TokenType::INSTANCEOF;
    else if (value == "try") type = TokenType::TRY;
    else if (value == "catch") type = TokenType::CATCH;
    else if (value == "finally") type = TokenType::FINALLY;
    else if (value == "throw") type = TokenType::THROW;
    else if (value == "enum") type = TokenType::ENUM;
    else if (value == "or") type = TokenType::OR_KW;
    else if (value == "propagate") type = TokenType::PROPAGATE;
    else if (value == "err") type = TokenType::ERR_KW;
    else if (value == "None") type = TokenType::NONE_KW;
    else if (value == "in") type = TokenType::IN;

    return Token(type, value, line, start_column, column, start_position, pos, filename);
}
