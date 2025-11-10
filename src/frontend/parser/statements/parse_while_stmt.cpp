#include "frontend/parser/statements/parse_while_stmt.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"

std::unique_ptr<Node> parse_while_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    parser->consume_token(); // 'while'

    auto condition = parse_logical_expr(parser);

    parser->expect(TokenType::OBRACE, "Expected '{'.");

    std::vector<std::unique_ptr<Stmt>> body;

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        auto stmt_node = parse_stmt(parser);

        auto* stmt_ptr = dynamic_cast<Stmt*>(stmt_node.get());

        body.push_back(std::unique_ptr<Stmt>(stmt_ptr));
        stmt_node.release();
    }

    parser->expect(TokenType::CBRACE, "Expected '}'.");

    auto while_node = std::make_unique<WhileStmtNode>(
        std::unique_ptr<Expr>(static_cast<Expr*>(condition.release())),
        std::move(body)
    );

    if (while_node && while_node->position) {
        pos->col[1] = while_node->position->col[1];
        pos->pos[1] = while_node->position->pos[1];
    }

    while_node->position = std::move(pos);

    return while_node;
}