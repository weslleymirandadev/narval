#include "frontend/parser/statements/parse_if_stmt.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"

std::unique_ptr<Node> parse_if_stmt(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    parser->consume_token(); // 'if'

    auto condition = parse_logical_expr(parser);

    parser->expect(TokenType::OBRACE, "Expected '{'.");

    auto if_node = std::make_unique<IfStatementNode>(
        std::unique_ptr<Expr>(static_cast<Expr*>(condition.release())),
        std::vector<std::unique_ptr<Stmt>>{},
        std::vector<std::unique_ptr<Stmt>>{}
    );

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        auto stmt = parse_stmt(parser);
        if_node->consequent.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
    }

    parser->expect(TokenType::CBRACE, "Expected '}'.");

    // Zero or more elif branches
    while (parser->not_eof() && parser->current_token().type == TokenType::ELIF) {
        parser->consume_token(); // 'elif'

        auto elif_condition = parse_logical_expr(parser);

        parser->expect(TokenType::OBRACE, "Expected '{'.");

        auto elif_node = std::make_unique<IfStatementNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(elif_condition.release())),
            std::vector<std::unique_ptr<Stmt>>{},
            std::vector<std::unique_ptr<Stmt>>{}
        );

        while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
            auto stmt = parse_stmt(parser);
            elif_node->consequent.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
        }

        parser->expect(TokenType::CBRACE, "Expected '}'.");

        if_node->alternate.push_back(std::move(elif_node));
    }

    // Handle zero or more "else if" and an optional final "else"
    if (parser->not_eof() && parser->current_token().type == TokenType::ELSE) {
        while (parser->not_eof() && parser->current_token().type == TokenType::ELSE) {
            parser->consume_token(); // 'else'

            if (parser->current_token().type == TokenType::IF) {
                parser->consume_token(); // 'if'

                auto else_if_condition = parse_logical_expr(parser);

                parser->expect(TokenType::OBRACE, "Expected '{'.");

                auto else_if_node = std::make_unique<IfStatementNode>(
                    std::unique_ptr<Expr>(static_cast<Expr*>(else_if_condition.release())),
                    std::vector<std::unique_ptr<Stmt>>{},
                    std::vector<std::unique_ptr<Stmt>>{}
                );

                while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
                    auto stmt = parse_stmt(parser);
                    else_if_node->consequent.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
                }

                parser->expect(TokenType::CBRACE, "Expected '}'.");

                if_node->alternate.push_back(std::move(else_if_node));

                // Continue looping: may have another 'else if' or a final 'else'
                if (!(parser->not_eof() && parser->current_token().type == TokenType::ELSE)) {
                    break;
                }
                continue;
            }

            // Final else block
            parser->expect(TokenType::OBRACE, "Expected '{'.");

            std::vector<std::unique_ptr<Stmt>> else_block;

            while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
                auto stmt = parse_stmt(parser);
                else_block.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(stmt.release())));
            }

            parser->expect(TokenType::CBRACE, "Expected '}'.");

            for (auto& s : else_block) {
                if_node->alternate.push_back(std::move(s));
            }
            break; // after a final else, stop
        }
    }

    if (if_node && if_node->position) {
        pos->col[1] = if_node->position->col[1];
        pos->pos[1] = if_node->position->pos[1];
    }

    if_node->position = std::move(pos);

    return if_node;
}