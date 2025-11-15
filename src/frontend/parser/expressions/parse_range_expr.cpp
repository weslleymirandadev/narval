#include "frontend/parser/expressions/parse_range_expr.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"

std::unique_ptr<Node> parse_range_expr(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    // Parse the left side as a normal logical expression
    auto left = parse_logical_expr(parser);

    // If there is a range operator, consume and parse the right side
    if (
        parser->current_token().type == TokenType::INCLUSIVE_RANGE ||
        parser->current_token().type == TokenType::RANGE
    ) {
        bool inclusive = (parser->current_token().type == TokenType::INCLUSIVE_RANGE);
        parser->consume_token();
        auto right = parse_logical_expr(parser);

        auto node = std::make_unique<RangeExprNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(left.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(right.release())),
            inclusive
        );

        if (right && right->position) {
            pos->col[1] = right->position->col[1];
            pos->pos[1] = right->position->pos[1];
        }
        node->position = std::move(pos);
        return node;
    }

    // No range: return the left expression
    if (left && !left->position) {
        left->position = std::move(pos);
    }
    return left;
}
