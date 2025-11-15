#include "frontend/parser/statements/parse_for_stmt.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/parser/expressions/parse_range_expr.hpp"
#include "frontend/ast/expressions/range_expr_node.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"

static std::vector<std::unique_ptr<Expr>> parse_binding_list(Parser* parser) {
    std::vector<std::unique_ptr<Expr>> bindings;

    auto first = parser->expect(TokenType::IDENTIFIER, "Expected identifier in for binding.");
    auto pos = std::make_unique<PositionData>(
        first.line,
        first.column_start, first.column_end,
        first.position_start, first.position_end
    );
    bindings.push_back(std::make_unique<IdentifierNode>(first.lexeme));

    while (parser->current_token().type == TokenType::COMMA) {
        parser->consume_token();
        auto id = parser->expect(TokenType::IDENTIFIER, "Expected identifier after ','.");
        auto id_pos = std::make_unique<PositionData>(
            id.line,
            id.column_start, id.column_end,
            id.position_start, id.position_end
        );
        bindings.push_back(std::make_unique<IdentifierNode>(id.lexeme));
    }

    return bindings;
}

std::unique_ptr<Node> parse_for_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    parser->consume_token(); // 'for'

    // --- Parse bindings or iterable ---
    std::vector<std::unique_ptr<Expr>> bindings;
    std::unique_ptr<Expr> range_start = nullptr;
    std::unique_ptr<Expr> range_end = nullptr;
    bool range_inclusive = false;
    std::unique_ptr<Expr> iterable = nullptr;

    bool has_bindings = false;
    if (parser->current_token().type == TokenType::IDENTIFIER) {
        if (parser->next_token().type == TokenType::COLON || parser->next_token().type == TokenType::COMMA) {
            has_bindings = true;
        }
    }

    if (has_bindings) {
        bindings = parse_binding_list(parser);  // retorna vector<unique_ptr<Expr>>
        parser->expect(TokenType::COLON, "Expected ':' after for bindings.");

        auto expr = parse_range_expr(parser);
        if (expr && expr->kind == NodeType::RangeExpression) {
            auto* r = static_cast<RangeExprNode*>(expr.get());
            range_inclusive = r->inclusive;
            range_start = std::unique_ptr<Expr>(static_cast<Expr*>(r->start->clone()));
            range_end = std::unique_ptr<Expr>(static_cast<Expr*>(r->end->clone()));
        } else {
            iterable = std::unique_ptr<Expr>(static_cast<Expr*>(expr.release()));
        }
    } else {
        auto it = parse_logical_expr(parser);
        iterable = std::unique_ptr<Expr>(static_cast<Expr*>(it.release()));
    }

    // --- Parse body ---
    parser->expect(TokenType::OBRACE, "Expected '{'.");

    std::vector<std::unique_ptr<Stmt>> body;
    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        auto stmt = parse_stmt(parser);
        body.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
    }
    parser->expect(TokenType::CBRACE, "Expected '}'.");

    // --- Parse else block (optional) ---
    std::vector<std::unique_ptr<Stmt>> else_block;
    if (parser->not_eof() && parser->current_token().type == TokenType::ELSE) {
        parser->consume_token();
        parser->expect(TokenType::OBRACE, "Expected '{' after 'else'.");
        while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
            auto stmt = parse_stmt(parser);
            else_block.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
        }
        parser->expect(TokenType::CBRACE, "Expected '}' after else block.");
    }

    // --- Construir o nó com construtor explícito ---
    auto for_node = std::make_unique<ForStmtNode>(
        std::move(bindings),
        std::move(range_start),
        std::move(range_end),
        range_inclusive,
        std::move(iterable),
        std::move(body),
        std::move(else_block)
    );

    // Atribuir posição (se aplicável)
    if (for_node->position == nullptr) {
        for_node->position = std::move(pos);
    }

    return for_node;
}