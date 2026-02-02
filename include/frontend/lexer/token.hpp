#pragma once
#include <string>
#include <cstddef>

enum class TokenType {
    TRUE,
    FALSE,
    IDENTIFIER,
    STRING,
    LOCK,
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
    LOOP,
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
    MOD_ASSIGN
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
        case TokenType::LOCK: return "LOCK";
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
        case TokenType::INCREMENT: return "INCREMENT";
        case TokenType::DECREMENT: return "DECREMENT";
        case TokenType::INTEGER_DIV: return "INTEGER DIV";
        case TokenType::MOD: return "MODULUS";
        case TokenType::FOR: return "FOR";
        case TokenType::WHILE: return "WHILE";
        case TokenType::LOOP: return "LOOP";
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

    Token(TokenType t, std::string l, size_t li, size_t cs, size_t ce, size_t ps, size_t pe, std::string f)
        : type(t), lexeme(l), line(li), column_start(cs), column_end(ce), position_start(ps), position_end(pe), filename(f) {}
};
