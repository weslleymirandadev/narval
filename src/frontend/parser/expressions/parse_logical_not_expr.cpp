#include "frontend/parser/expressions/parse_logical_not_expr.hpp"
#include "frontend/parser/expressions/parse_unary_expr.hpp"

std::unique_ptr<Node> parse_logical_not_expr(Parser* parser) {
    if (parser->current_token().type == TokenType::NOT) {
        size_t line = parser->current_token().line;
        size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
        size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
        std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

        parser->consume_token();

        auto operand = parse_logical_not_expr(parser);

        auto not_node = std::make_unique<LogicalNotExprNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(operand.release()))
        );

        if (operand && operand->position) {
            pos->col[1] = operand->position->col[1];
            pos->pos[1] = operand->position->pos[1];
        }

        not_node->position = std::move(pos);

        return not_node;
    } else {
        return parse_unary_expr(parser);
    }
}