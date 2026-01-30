#include "frontend/parser/statements/parse_continue_stmt.hpp"

std::unique_ptr<Node> parse_continue_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    
    parser->consume_token(); // 'continue'
    parser->expect(TokenType::SEMICOLON, "Expected ';' after continue.");
    
    if (pos) {
        pos->col[1] = parser->current_token().column_end - 1;
        pos->pos[1] = parser->current_token().position_end - 1;
    }

    auto node = std::make_unique<ContinueStmtNode>();
    node->position = std::move(pos);
    return node;
}
