#include "frontend/parser/statements/parse_extern_stmt.hpp"
#include "frontend/parser/expressions/parse_args.hpp"
#include "frontend/parser/expressions/parse_type.hpp"
#include "frontend/ast/statements/extern_stmt_node.hpp"

// Parse: `extern "<lang>" { def name(params): ret; ... }`
// Funções declaradas não têm corpo — apenas assinatura.
std::unique_ptr<Node> parse_extern_stmt(Parser* parser) {
    parser->consume_token(); // consume 'extern'

    if (parser->current_token().type != TokenType::STRING) {
        parser->error("Expected language string after 'extern' (e.g., extern \"C\")");
        return nullptr;
    }
    std::string language = parser->current_token().lexeme;
    parser->consume_token(); // consume language string

    // `extern "Python" from "arquivo.py" { ... }` — source file opcional
    std::string source_file;
    if (parser->current_token().type == TokenType::FROM) {
        parser->consume_token(); // consume 'from'
        if (parser->current_token().type != TokenType::STRING) {
            parser->error("Expected Python source file path after 'from'");
            return nullptr;
        }
        source_file = parser->current_token().lexeme;
        parser->consume_token(); // consume path string
    }

    parser->expect(TokenType::OBRACE, "Expected '{' after extern language specifier");

    std::vector<ExternFuncDecl> decls;

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        if (parser->current_token().type != TokenType::DEF) {
            parser->error("Expected 'def' inside extern block");
            parser->consume_token();
            continue;
        }
        parser->consume_token(); // consume 'def'

        auto name_tok = parser->expect(TokenType::IDENTIFIER, "Expected function name in extern declaration");
        parser->expect(TokenType::OPAREN, "Expected '(' after function name");

        std::vector<ParamNode> params;
        while (parser->not_eof() && parser->current_token().type != TokenType::CPAREN) {
            auto arg_name = parser->expect(TokenType::IDENTIFIER, "Expected parameter name");
            parser->expect(TokenType::COLON, "Expected ':' after parameter name");
            std::string arg_type = parse_type(parser);

            std::unordered_map<std::string, std::string> param_map;
            param_map[arg_name.lexeme] = arg_type;
            params.emplace_back(param_map, nullptr);

            if (parser->current_token().type == TokenType::COMMA)
                parser->consume_token();
            else if (parser->current_token().type != TokenType::CPAREN)
                parser->expect(TokenType::COMMA, "Expected ',' or ')' in extern parameter list");
        }
        parser->expect(TokenType::CPAREN, "Expected ')' after extern parameters");

        std::string ret_type = "void";
        if (parser->current_token().type == TokenType::COLON) {
            parser->consume_token();
            ret_type = parse_type(parser);
        }

        if (parser->current_token().type == TokenType::SEMICOLON)
            parser->consume_token();

        decls.emplace_back(name_tok.lexeme, std::move(params), ret_type);
    }

    parser->expect(TokenType::CBRACE, "Expected '}' after extern block");

    auto node = std::make_unique<ExternStmtNode>(language, std::move(decls));
    node->source_file = source_file;
    return node;
}
