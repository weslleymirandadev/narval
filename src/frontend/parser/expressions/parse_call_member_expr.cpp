#include "frontend/parser/expressions/parse_access_expr.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"
#include "frontend/parser/expressions/parse_call_expr.hpp"
#include "frontend/parser/expressions/parse_member_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"

std::unique_ptr<Node> parse_call_member_expr(Parser* parser, std::unique_ptr<Node> statement) {
    std::unique_ptr<Node> expr;

    if (statement) {
        expr = std::move(statement);
    } else {
        expr = parse_member_expr(parser);
    }

    // Loop to allow chaining: calls, array access, and member access in any order
    while (
        parser->current_token().type == TokenType::OPAREN ||
        parser->current_token().type == TokenType::OBRACKET ||
        parser->current_token().type == TokenType::DOT
    ) {
        if (parser->current_token().type == TokenType::OPAREN) {
            expr = parse_call_expr(parser, std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())));
            continue;
        }

        if (parser->current_token().type == TokenType::OBRACKET) {
            expr = parse_access_expr(parser, std::move(expr));
            continue;
        }

        // Member access via '.'
        if (parser->current_token().type == TokenType::DOT) {
            parser->consume_token();

            size_t line_property = parser->current_token().line;
            size_t column_property[2] = { parser->current_token().column_start, parser->current_token().column_end };
            size_t position_property[2] = { parser->current_token().position_start, parser->current_token().position_end };
            std::unique_ptr<PositionData> pos_property = std::make_unique<PositionData>(line_property, column_property[0], column_property[1], position_property[0], position_property[1]);

            auto property = parse_primary_expr(parser);

            auto member = std::make_unique<MemberExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
                std::unique_ptr<Expr>(static_cast<Expr*>(property.release()))
            );

            if (member && member->position) {
                pos_property->col[1] = member->position->col[1];
                pos_property->pos[1] = member->position->pos[1];
            }

            member->position = std::move(pos_property);
            expr = std::move(member);
            continue;
        }
    }

    return expr;
}