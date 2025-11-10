#include "frontend/parser/expressions/parse_call_expr.hpp"
#include "frontend/parser/expressions/parse_args.hpp"

std::unique_ptr<Node> parse_call_expr(Parser* parser, std::unique_ptr<Expr> caller) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    auto args = parse_args(parser);

    auto call_node = std::make_unique<CallExprNode>(std::move(caller), std::move(args));

    if (parser->current_token().type == TokenType::OPAREN) {
        std::unique_ptr<Expr> callee_expr = std::move(call_node);
        auto result_node = parse_call_expr(parser, std::move(callee_expr));
        call_node.reset(static_cast<CallExprNode*>(result_node.release()));
    }

    if (call_node && call_node->position) {
        pos->col[1] = call_node->position->col[1];
        pos->pos[1] = call_node->position->pos[1];
    }

    call_node->position = std::move(pos);
    return call_node;
}