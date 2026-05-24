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

    // None como tipo de retorno (equivale a void)
    if (curr.type == TokenType::NONE_KW) {
        parser->consume_token();
        return "None";
    }

    // 1. Pode começar com: IDENTIFIER, OBRACKET, ou OPAREN
    if (curr.type == TokenType::IDENTIFIER) {
        Token base = parser->consume_token();
        type_str = base.lexeme;
        if (type_str == "void") {
            parser->error("'void' is not a valid type — use 'None'");
            return "void"; // recover
        }

        // Option<T>
        if (type_str == "Option") {
            parser->expect(TokenType::LT, "Expected '<' after Option");
            std::string inner = parse_type(parser);
            parser->expect(TokenType::GT, "Expected '>' after Option type");
            return "Option<" + inner + ">";
        }

        // Result<T, E>
        if (type_str == "Result") {
            parser->expect(TokenType::LT, "Expected '<' after Result");
            std::string ok_t = parse_type(parser);
            parser->expect(TokenType::COMMA, "Expected ',' in Result<T, E>");
            std::string err_t = parse_type(parser);
            parser->expect(TokenType::GT, "Expected '>' after Result types");
            return "Result<" + ok_t + ", " + err_t + ">";
        }

        // Future<T>
        if (type_str == "Future") {
            parser->expect(TokenType::LT, "Expected '<' after Future");
            std::string inner = parse_type(parser);
            parser->expect(TokenType::GT, "Expected '>' after Future type");
            return "Future<" + inner + ">";
        }

        // Tensor<elem_type, [d0, d1, ...]>
        // Example: Tensor<float, [32, 128]>  or  Tensor<float, [-1, -1]>
        if (type_str == "Tensor") {
            parser->expect(TokenType::LT, "Expected '<' after Tensor");
            std::string elem = parse_type(parser);
            parser->expect(TokenType::COMMA, "Expected ',' in Tensor<T, [...]>");
            parser->expect(TokenType::OBRACKET, "Expected '[' for Tensor shape");
            // Parse comma-separated dimension list
            std::string shape_str;
            while (parser->current_token().type != TokenType::CBRACKET) {
                Token dim_tok = parser->current_token();
                if (dim_tok.type == TokenType::NUMBER) {
                    shape_str += dim_tok.lexeme;
                    parser->consume_token();
                } else if (dim_tok.type == TokenType::MINUS) {
                    parser->consume_token(); // consume '-'
                    Token n = parser->consume_token(); // consume '1'
                    shape_str += "-" + n.lexeme;
                } else if (dim_tok.type == TokenType::IDENTIFIER && dim_tok.lexeme == "?") {
                    shape_str += "-1";
                    parser->consume_token();
                } else {
                    parser->error("Expected integer dimension in Tensor shape");
                    break;
                }
                if (parser->current_token().type == TokenType::COMMA) {
                    shape_str += ", ";
                    parser->consume_token();
                }
            }
            parser->expect(TokenType::CBRACKET, "Expected ']' after Tensor shape");
            parser->expect(TokenType::GT, "Expected '>' after Tensor<T,[...]>");
            return "Tensor<" + elem + ", [" + shape_str + "]>";
        }

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
            type_str = "vector";
        }
        // 4. Array type: int[10], string[5] (tamanho fixo > 0)
        else if (parser->current_token().type == TokenType::OBRACKET) {
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
        // 5. Generic user type: Box<T>, Container<K, V>, etc.
        else if (parser->current_token().type == TokenType::LT) {
            parser->consume_token(); // <
            std::vector<std::string> type_args;
            bool first = true;
            while (parser->not_eof() && parser->current_token().type != TokenType::GT) {
                if (!first) parser->expect(TokenType::COMMA, "Expected ',' in type arguments");
                first = false;
                type_args.push_back(parse_type(parser));
            }
            parser->expect(TokenType::GT, "Expected '>' after type arguments");
            type_str += "<" + join(type_args, ", ") + ">";
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
        // Tuple type: (int, str)
        parser->consume_token(); // (
        std::vector<std::string> elems;
        bool first = true;
        while (parser->current_token().type != TokenType::CPAREN &&
               parser->current_token().type != TokenType::EOF_TOKEN) {
            if (!first) parser->expect(TokenType::COMMA, "Expected ',' in tuple type.");
            first = false;
            elems.push_back(parse_type(parser));
        }
        parser->expect(TokenType::CPAREN, "Expected ')' to close tuple type.");
        type_str = "(" + join(elems, ", ") + ")";
    }
    else if (curr.type == TokenType::BITWISE_OR) {
        // Function type: |name: type, ...|: ret
        // Use the same string format as create_function_type_string so the checker can parse it.
        parser->consume_token(); // '|'
        std::vector<std::pair<std::string, std::string>> params;
        while (parser->current_token().type != TokenType::BITWISE_OR &&
               parser->current_token().type != TokenType::EOF_TOKEN) {
            std::string pname = parser->expect(TokenType::IDENTIFIER, "Expected parameter name in function type").lexeme;
            parser->expect(TokenType::COLON, "Expected ':' after parameter name in function type");
            std::string ptype = parse_type(parser);
            params.emplace_back(pname, ptype);
            if (parser->current_token().type == TokenType::COMMA)
                parser->consume_token();
            else if (parser->current_token().type != TokenType::BITWISE_OR)
                break;
        }
        parser->expect(TokenType::BITWISE_OR, "Expected '|' to close function type parameters");
        parser->expect(TokenType::COLON, "Expected ':' after '|' in function type");
        std::string ret = parse_type(parser);
        std::string result = "|";
        for (size_t i = 0; i < params.size(); ++i) {
            result += params[i].first + ":" + params[i].second;
            if (i + 1 < params.size()) result += ", ";
        }
        result += "|:" + ret;
        type_str = result;
    }
    else {
        parser->error("Invalid type start: expected identifier, '[', '(', or '|name: type|: ret' for function types");
    }

    return type_str;
}