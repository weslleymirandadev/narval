#include "frontend/parser/expressions/parse_assignment_expr.hpp"
#include "frontend/parser/expressions/parse_arguments_list.hpp"

std::vector<std::unique_ptr<ArgNode>> parse_arguments_list(Parser* parser) {
    auto args = std::vector<std::unique_ptr<ArgNode>>{};

    while (parser->current_token().type != TokenType::CPAREN) {
        // Keyword args: `identifier = expr` (or `identifier: expr`, which the
        // specs use for import_c's `link:` and cannot appear in any other valid
        // argument position).
        if (parser->current_token().type == TokenType::IDENTIFIER) {
            auto look = parser->next_token();
            if (look.type == TokenType::ASSIGNMENT || look.type == TokenType::COLON) {
                // Keyword arg
                std::string name = parser->current_token().lexeme;
                parser->consume_token(); // identifier
                parser->expect(look.type, look.type == TokenType::COLON
                    ? "Expected ':' after keyword argument name."
                    : "Expected '=' after keyword argument name.");
                // expect() already consumes the separator, do not consume again
                auto val = parse_assignment_expr(parser);
                args.push_back(std::make_unique<ArgNode>(name, std::unique_ptr<Expr>(static_cast<Expr*>(val.release()))));
                if (parser->current_token().type == TokenType::COMMA) parser->consume_token();
                else break;
                continue;
            }
        }

        auto arg = parse_assignment_expr(parser);
        args.push_back(std::make_unique<ArgNode>(std::string(), std::unique_ptr<Expr>(static_cast<Expr*>(arg.release()))));

        if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        } else {
            break;
        }
    }

    return args;
}