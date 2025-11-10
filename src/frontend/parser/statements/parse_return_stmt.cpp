#include "frontend/parser/statements/parse_return_stmt.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"

std::unique_ptr<Node> parse_return_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    
    parser->consume_token(); // 'return'
    
    auto value = parse_expr(parser);
    parser->expect(TokenType::SEMICOLON, "Expected ';'.");
    if (value && value->position) {
        pos->col[1] = value->position->col[1];
        pos->pos[1] = value->position->pos[1];
    }

    auto node = std::make_unique<ReturnStmtNode>(std::unique_ptr<Expr>(static_cast<Expr*>(value.release())));
    node->position = std::move(pos);
    return node;
}
