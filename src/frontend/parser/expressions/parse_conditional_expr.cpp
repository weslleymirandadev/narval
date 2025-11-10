#include "frontend/parser/expressions/parse_conditional_expr.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"

std::unique_ptr<Node> parse_conditional_expr(Parser* parser, std::unique_ptr<Node> value) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    parser->consume_token();

    auto condition = parse_logical_expr(parser);

    parser->expect(TokenType::ELSE, "Expected Else.");

    auto false_value = parse_expr(parser);
    
    // Check if there's another conditional chain before creating the node
    if (parser->current_token().type == TokenType::IF) {
        false_value = parse_conditional_expr(parser, std::move(false_value));
    }

    auto cond_node = std::make_unique<ConditionalExprNode>(
       std::unique_ptr<Expr>(static_cast<Expr*>(value.release())),
       std::unique_ptr<Expr>(static_cast<Expr*>(condition.release())),
       std::unique_ptr<Expr>(static_cast<Expr*>(false_value.release()))
    );

    if (false_value && false_value->position) {
        pos->col[1] = false_value->position->col[1];
        pos->pos[1] = false_value->position->pos[1];
    }

    cond_node->position = std::move(pos);

    return cond_node;
}