#include "frontend/parser/statements/parse_forever_stmt.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_block_util.hpp"

std::unique_ptr<Node> parse_forever_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    parser->consume_token(); // 'forever'
    parser->expect(TokenType::OBRACE, "Expected '{'.");
    auto body = parse_body(parser);
    parser->expect(TokenType::CBRACE, "Expected '}'.");

    auto forever_node = std::make_unique<ForeverStmtNode>(std::move(body));

    if (forever_node && forever_node->position) {
        pos->col[1] = forever_node->position->col[1];
        pos->pos[1] = forever_node->position->pos[1];
    }

    forever_node->position = std::move(pos);

    return forever_node;
}