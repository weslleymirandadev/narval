#pragma once
#include <string>
#include <cstddef>

enum class TokenType {
    TRUE,
    FALSE,
    IDENTIFIER,
    STRING,
    RETURN,
    NUMBER,
    ASSIGNMENT,
    SEMICOLON,
    IF,
    COLON,
    ELIF,
    ELSE,
    MATCH,
    FOR,
    WHILE,
    FOREVER,
    BREAK,
    CONTINUE,
    UNDERSCORE,
    ELIPSIS,
    IMPORT,
    DEF,
    INCLUSIVE_RANGE,
    RANGE,
    ARROW,
    PLUS,
    MINUS,
    MUL,
    DIV,
    POWER,
    EQUALS,
    DIFFERENT,
    LESS_THAN_EQUALS,
    GREATER_THAN_EQUALS,
    LT,
    GT,
    AND,
    OR,
    DOT,
    NOT,
    INCREMENT,
    DECREMENT,
    OPAREN,
    CPAREN,
    OBRACE,
    CBRACE,
    OBRACKET,
    CBRACKET,
    COMMA,
    MOD,
    INTEGER_DIV,
    EOF_TOKEN,
    UNKNOWN,
    PLUS_ASSIGN,
    MUL_ASSIGN,
    MINUS_ASSIGN,
    DIV_ASSIGN,
    INTEGER_DIV_ASSIGN,
    POWER_ASSIGN, //that's kinda poetic
    MOD_ASSIGN,
    BITWISE_AND,
    BITWISE_OR,
    BITWISE_XOR,
    BITWISE_NOT,
    LEFT_SHIFT,
    RIGHT_SHIFT,
    BITWISE_AND_ASSIGN,
    BITWISE_OR_ASSIGN,
    BITWISE_XOR_ASSIGN,
    LEFT_SHIFT_ASSIGN,
    RIGHT_SHIFT_ASSIGN,
    AT,
    AT_ASSIGN,
    IN,
    CLASS,
    PUBLIC,
    PRIVATE,
    PROTECTED,
    ABSTRACT,
    EXTENDS,
    IMPLEMENTS,
    INTERFACE,
    NEW,
    SELF,
    SUPER,
    INSTANCEOF,
    MUT,
    TRY,
    CATCH,
    FINALLY,
    THROW,
    ENUM,
    OR_KW,
    PROPAGATE,
    ERR_KW,
    NONE_KW,
    DEFER,
    EXTERN,
    FROM,
    AS,
};

inline const char* get_token_name(TokenType type) {
    switch (type) {
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::STRING: return "STRING";
        case TokenType::OPAREN: return "OPAREN";
        case TokenType::CPAREN: return "CPAREN";
        case TokenType::OBRACE: return "OBRACE";
        case TokenType::CBRACE: return "CBRACE";
        case TokenType::OBRACKET: return "OBRACKET";
        case TokenType::CBRACKET: return "CBRACKET";
        case TokenType::COMMA: return "COMMA";
        case TokenType::RETURN: return "RETURN";
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::ASSIGNMENT: return "ASSIGNMENT";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::COLON: return "COLON";
        case TokenType::IF: return "IF";
        case TokenType::DEF: return "DEF";
        case TokenType::ELIF: return "ELIF";
        case TokenType::ELSE: return "ELSE";
        case TokenType::MATCH: return "MATCH";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::AS: return "AS";
        case TokenType::INCREMENT: return "INCREMENT";
        case TokenType::DECREMENT: return "DECREMENT";
        case TokenType::INTEGER_DIV: return "INTEGER DIV";
        case TokenType::MOD: return "MODULUS";
        case TokenType::FOR: return "FOR";
        case TokenType::WHILE: return "WHILE";
        case TokenType::FOREVER: return "FOREVER";
        case TokenType::BREAK: return "BREAK";
        case TokenType::CONTINUE: return "CONTINUE";
        case TokenType::UNDERSCORE: return "UNDERSCORE";
        case TokenType::ELIPSIS: return "ELIPSIS";
        case TokenType::IMPORT: return "IMPORT";
        case TokenType::INCLUSIVE_RANGE: return "INCLUSIVE_RANGE";
        case TokenType::RANGE: return "RANGE";
        case TokenType::EQUALS: return "EQUALS";
        case TokenType::DIFFERENT: return "DIFFERENT";
        case TokenType::LESS_THAN_EQUALS: return "LESS_THAN_EQUALS";
        case TokenType::GREATER_THAN_EQUALS: return "GREATER_THAN_EQUALS";
        case TokenType::ARROW: return "ARROW";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::MUL: return "MUL";
        case TokenType::DIV: return "DIV";
        case TokenType::LT: return "LT";
        case TokenType::GT: return "GT";
        case TokenType::DOT: return "DOT";
        case TokenType::NOT: return "NOT";
        case TokenType::POWER: return "POWER";
        case TokenType::EOF_TOKEN: return "EOF_TOKEN";
        case TokenType::TRUE: return "TRUE";
        case TokenType::FALSE: return "FALSE";
        case TokenType::PLUS_ASSIGN: return "PLUS_ASSIGN";
        case TokenType::MINUS_ASSIGN: return "MINUS_ASSIGN";
        case TokenType::MUL_ASSIGN: return "MUL_ASSIGN";
        case TokenType::DIV_ASSIGN: return "DIV_ASSIGN";
        case TokenType::INTEGER_DIV_ASSIGN: return "INTEGER_DIV_ASSIGN";
        case TokenType::POWER_ASSIGN: return "POWER_ASSIGN";
        case TokenType::MOD_ASSIGN: return "MOD_ASSIGN";
        case TokenType::BITWISE_AND: return "BITWISE_AND";
        case TokenType::BITWISE_OR: return "BITWISE_OR";
        case TokenType::BITWISE_XOR: return "BITWISE_XOR";
        case TokenType::BITWISE_NOT: return "BITWISE_NOT";
        case TokenType::LEFT_SHIFT: return "LEFT_SHIFT";
        case TokenType::RIGHT_SHIFT: return "RIGHT_SHIFT";
        case TokenType::BITWISE_AND_ASSIGN: return "BITWISE_AND_ASSIGN";
        case TokenType::BITWISE_OR_ASSIGN: return "BITWISE_OR_ASSIGN";
        case TokenType::BITWISE_XOR_ASSIGN: return "BITWISE_XOR_ASSIGN";
        case TokenType::LEFT_SHIFT_ASSIGN: return "LEFT_SHIFT_ASSIGN";
        case TokenType::RIGHT_SHIFT_ASSIGN: return "RIGHT_SHIFT_ASSIGN";
        case TokenType::AT: return "AT";
        case TokenType::AT_ASSIGN: return "AT_ASSIGN";
        case TokenType::IN: return "IN";
        case TokenType::CLASS: return "CLASS";
        case TokenType::PUBLIC: return "PUBLIC";
        case TokenType::PRIVATE: return "PRIVATE";
        case TokenType::PROTECTED: return "PROTECTED";
        case TokenType::ABSTRACT: return "ABSTRACT";
        case TokenType::EXTENDS: return "EXTENDS";
        case TokenType::IMPLEMENTS: return "IMPLEMENTS";
        case TokenType::INTERFACE: return "INTERFACE";
        case TokenType::NEW: return "NEW";
        case TokenType::SELF: return "SELF";
        case TokenType::SUPER: return "SUPER";
        case TokenType::INSTANCEOF: return "INSTANCEOF";
        case TokenType::MUT: return "MUT";
        case TokenType::TRY: return "TRY";
        case TokenType::CATCH: return "CATCH";
        case TokenType::FINALLY: return "FINALLY";
        case TokenType::THROW: return "THROW";
        case TokenType::ENUM: return "ENUM";
        case TokenType::OR_KW: return "OR_KW";
        case TokenType::PROPAGATE: return "PROPAGATE";
        case TokenType::ERR_KW: return "ERR_KW";
        case TokenType::NONE_KW: return "NONE_KW";
        case TokenType::DEFER: return "DEFER";
        case TokenType::EXTERN: return "EXTERN";
        case TokenType::FROM: return "FROM";
        default: return "UNKNOWN";
    }
}

struct Token {
    TokenType type;
    std::string lexeme;
    size_t line;
    size_t column_start;
    size_t column_end;
    size_t position_start;
    size_t position_end;
    std::string filename;
    char delimiter;

    Token(TokenType t, std::string l, size_t li, size_t cs, size_t ce, size_t ps, size_t pe, std::string f, char d = '\0')
        : type(t), lexeme(l), line(li), column_start(cs), column_end(ce), position_start(ps), position_end(pe), filename(f), delimiter(d) {}
};
