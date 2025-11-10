#include "frontend/parser/expressions/parse_multiplicative_expr.hpp"
#include "frontend/parser/expressions/parse_power_expr.hpp"

std::unique_ptr<Node> parse_multiplicative_expr(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    auto left = parse_power_expr(parser);
    
    while (
        parser->current_token().type == TokenType::MUL ||
        parser->current_token().type == TokenType::DIV ||
        parser->current_token().type == TokenType::MOD ||
        parser->current_token().type == TokenType::INTEGER_DIV
    ) {
        std::string opToken = parser->consume_token().lexeme;
        auto right = parse_power_expr(parser);
        
        auto binaryNode = std::make_unique<BinaryExprNode>(
            opToken,
            std::unique_ptr<Expr>(static_cast<Expr*>(left.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
        );

        if (right && right->position) {
            pos->col[1] = right->position->col[1];
            pos->pos[1] = right->position->pos[1];
        }

        binaryNode->position = std::move(pos);

        left = std::move(binaryNode);
    }
    return left;
}