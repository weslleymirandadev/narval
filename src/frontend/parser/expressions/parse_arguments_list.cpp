#include "frontend/parser/expressions/parse_assignment_expr.hpp"
#include "frontend/parser/expressions/parse_arguments_list.hpp"

std::vector<std::unique_ptr<Expr>> parse_arguments_list(Parser* parser) {
    auto args = std::vector<std::unique_ptr<Expr>>{};

    while (parser->current_token().type != TokenType::CPAREN) {
        auto arg = parse_assignment_expr(parser);
        args.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(arg.release())));

        if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        } else {
            break;
        }
    }

    return args;
}