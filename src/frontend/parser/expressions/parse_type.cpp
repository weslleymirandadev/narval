#include "frontend/parser/expressions/parse_type.hpp"
#include <vector>
#include <sstream>

// Helper: join para tuplas
static std::string join(const std::vector<std::string>& v, const std::string& sep) {
    if (v.empty()) return "";
    std::string res = v[0];
    for (size_t i = 1; i < v.size(); ++i) res += sep + v[i];
    return res;
}

std::string parse_type(Parser* parser) {
    std::string type_str;

    Token curr = parser->current_token();

    // 1. Pode começar com: IDENTIFIER, OBRACKET, ou OPAREN
    if (curr.type == TokenType::IDENTIFIER) {
        Token base = parser->consume_token();
        type_str = base.lexeme;

        // 2. Generics: vec<T>, map<K,V>
        if (type_str == "vec" || type_str == "map") {
            parser->expect(TokenType::LT, "Expected '<' after vec/map.");
            std::string inner1 = parse_type(parser);
            if (type_str == "map") {
                parser->expect(TokenType::COMMA, "Expected ',' in map<K,V>.");
                std::string inner2 = parse_type(parser);
                type_str = "map<" + inner1 + ", " + inner2 + ">";
            } else {
                type_str = "vec<" + inner1 + ">";
            }
            parser->expect(TokenType::GT, "Expected '>' after generic.");
        }
    }
    else if (curr.type == TokenType::OBRACKET) {
        // Array fixo: [3]int
        parser->consume_token(); // [
        Token num = parser->consume_token();
        if (num.type != TokenType::NUMBER) {
            throw std::runtime_error("Expected number in array size.");
        }
        parser->expect(TokenType::CBRACKET, "Expected ']' after array size.");
        std::string elem = parse_type(parser);
        type_str = "[" + num.lexeme + "]" + elem;
    }
    else if (curr.type == TokenType::OPAREN) {
        // Tuple: (int, str, int)
        parser->consume_token(); // (
        std::vector<std::string> elems;
        bool first = true;
        while (parser->current_token().type != TokenType::CPAREN) {
            if (!first) parser->expect(TokenType::COMMA, "Expected ',' in tuple.");
            first = false;
            std::string elem_type = parse_type(parser);
            elems.push_back(elem_type);
        }
        parser->expect(TokenType::CPAREN, "Expected ')' to close tuple.");
        type_str = "(" + join(elems, ", ") + ")";
    }
    else {
        throw std::runtime_error("Invalid type start: expected identifier, '[', or '('");
    }

    return type_str;
}