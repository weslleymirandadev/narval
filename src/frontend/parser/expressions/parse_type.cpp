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

        // 2. Generics: map<K,V>
        if (type_str == "map") {
            parser->expect(TokenType::LT, "Expected '<' after map.");
            std::string inner1 = parse_type(parser);
            parser->expect(TokenType::COMMA, "Expected ',' in map<K,V>.");
            std::string inner2 = parse_type(parser);
            type_str = "map<" + inner1 + ", " + inner2 + ">";
            parser->expect(TokenType::GT, "Expected '>' after generic.");
        }
        // 3. Vector type: vector (sem generics, lista heterogênea)
        else if (type_str == "vector") {
            // vector é um tipo simples, sem modificadores
            type_str = "vector";
        }
        // 4. Array type: int[10], string[5] (tamanho fixo > 0)
        else if (parser->current_token().type == TokenType::OBRACKET) {
            // Verificar se é array com tamanho: int[10]
            parser->consume_token(); // [
            Token num = parser->consume_token();
            if (num.type != TokenType::NUMBER) {
                parser->error("Expected number > 0 in array size. Use 'vector' for dynamic lists.");
            }
            int size = std::stoi(num.lexeme);
            if (size <= 0) {
                parser->error("Array size must be greater than 0. Use 'vector' for dynamic lists.");
            }
            parser->expect(TokenType::CBRACKET, "Expected ']' after array size.");
            type_str = type_str + "[" + num.lexeme + "]";
        }
    }
    else if (curr.type == TokenType::OBRACKET) {
        // Array fixo: [3]int (sintaxe antiga, ainda suportada)
        parser->consume_token(); // [
        Token num = parser->consume_token();
        if (num.type != TokenType::NUMBER) {
            parser->error("Expected number > 0 in array size. Use 'vector' for dynamic lists.");
        }
        int size = std::stoi(num.lexeme);
        if (size <= 0) {
            parser->error("Array size must be greater than 0. Use 'vector' for dynamic lists.");
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
        parser->error("Invalid type start: expected identifier, '[', or '('");
    }

    return type_str;
}