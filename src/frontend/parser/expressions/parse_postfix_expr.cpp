#include "frontend/parser/expressions/parse_postfix_expr.hpp"

std::unique_ptr<Node> parse_postfix_expr(Parser* parser, std::unique_ptr<Node> expr) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    std::unique_ptr<Node> exp;

    switch (parser->current_token().type) {
        case TokenType::INCREMENT: {
            parser->consume_token();

            auto unary_node = std::make_unique<PostIncrementExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(expr.release()))
            );

            unary_node->position = std::move(pos);

            exp = std::move(unary_node);
            break;
        }
        case TokenType::DECREMENT: {
            parser->consume_token();

            auto unary_node = std::make_unique<PostDecrementExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(expr.release()))
            );

            unary_node->position = std::move(pos);

            exp = std::move(unary_node);
            break;
        }
    }

    return exp;
}