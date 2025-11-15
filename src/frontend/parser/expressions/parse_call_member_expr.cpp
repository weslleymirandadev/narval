#include "frontend/parser/expressions/parse_access_expr.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"
#include "frontend/parser/expressions/parse_call_expr.hpp"
#include "frontend/parser/expressions/parse_member_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/expressions/parse_power_expr.hpp"
#include "frontend/parser/expressions/parse_multiplicative_expr.hpp"
#include "frontend/parser/expressions/parse_additive_expr.hpp"
#include "frontend/parser/expressions/parse_logical_not_expr.hpp"
#include "frontend/parser/expressions/parse_relational_expr.hpp"
#include "frontend/parser/expressions/parse_equality_expr.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"

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

    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    while (parser->current_token().type == TokenType::POWER) {
        std::string opToken = parser->consume_token().lexeme;
        auto right = parse_call_member_expr(parser, nullptr);

        auto binaryNode = std::make_unique<BinaryExprNode>(
            opToken,
            std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
        );

        if (right && right->position) {
            pos->col[1] = right->position->col[1];
            pos->pos[1] = right->position->pos[1];
        }

        binaryNode->position = std::move(pos);

        expr = std::move(binaryNode);

        line = parser->current_token().line;
        column[0] = parser->current_token().column_start; column[1] = parser->current_token().column_end;
        position[0] = parser->current_token().position_start; position[1] = parser->current_token().position_end;
        pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    }

    while (
        parser->current_token().type == TokenType::MUL ||
        parser->current_token().type == TokenType::DIV ||
        parser->current_token().type == TokenType::MOD ||
        parser->current_token().type == TokenType::INTEGER_DIV
    ) {
        std::string opToken = parser->consume_token().lexeme;
        auto right = parse_power_expr(parser);

        auto binaryNode = std::make_unique<BinaryExprNode>(
            opToken,
            std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
        );

        if (right && right->position) {
            pos->col[1] = right->position->col[1];
            pos->pos[1] = right->position->pos[1];
        }

        binaryNode->position = std::move(pos);
        expr = std::move(binaryNode);

        line = parser->current_token().line;
        column[0] = parser->current_token().column_start; column[1] = parser->current_token().column_end;
        position[0] = parser->current_token().position_start; position[1] = parser->current_token().position_end;
        pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    }

    while (
        parser->current_token().type == TokenType::PLUS ||
        parser->current_token().type == TokenType::MINUS
    ) {
        std::string opToken = parser->consume_token().lexeme;
        auto right = parse_multiplicative_expr(parser);

        auto binaryNode = std::make_unique<BinaryExprNode>(
            opToken,
            std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
        );

        if (right && right->position) {
            pos->col[1] = right->position->col[1];
            pos->pos[1] = right->position->pos[1];
        }

        binaryNode->position = std::move(pos);
        expr = std::move(binaryNode);

        line = parser->current_token().line;
        column[0] = parser->current_token().column_start; column[1] = parser->current_token().column_end;
        position[0] = parser->current_token().position_start; position[1] = parser->current_token().position_end;
        pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    }

    // Relational: <, <=, >, >=
    while (
        parser->current_token().type == TokenType::LESS_THAN_EQUALS ||
        parser->current_token().type == TokenType::GREATER_THAN_EQUALS ||
        parser->current_token().type == TokenType::LT ||
        parser->current_token().type == TokenType::GT
    ) {
        std::string opToken = parser->consume_token().lexeme;
        auto right = parse_logical_not_expr(parser);

        auto binaryNode = std::make_unique<BinaryExprNode>(
            opToken,
            std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
        );

        if (right && right->position) {
            pos->col[1] = right->position->col[1];
            pos->pos[1] = right->position->pos[1];
        }

        binaryNode->position = std::move(pos);
        expr = std::move(binaryNode);

        line = parser->current_token().line;
        column[0] = parser->current_token().column_start; column[1] = parser->current_token().column_end;
        position[0] = parser->current_token().position_start; position[1] = parser->current_token().position_end;
        pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    }

    // Equality: ==, !=
    while (
        parser->current_token().type == TokenType::EQUALS ||
        parser->current_token().type == TokenType::DIFFERENT
    ) {
        std::string opToken = parser->consume_token().lexeme;
        auto right = parse_relational_expr(parser);

        auto binaryNode = std::make_unique<BinaryExprNode>(
            opToken,
            std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
        );

        if (right && right->position) {
            pos->col[1] = right->position->col[1];
            pos->pos[1] = right->position->pos[1];
        }

        binaryNode->position = std::move(pos);
        expr = std::move(binaryNode);

        line = parser->current_token().line;
        column[0] = parser->current_token().column_start; column[1] = parser->current_token().column_end;
        position[0] = parser->current_token().position_start; position[1] = parser->current_token().position_end;
        pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    }

    // Logical: &&, ||
    while (
        parser->current_token().type == TokenType::AND ||
        parser->current_token().type == TokenType::OR
    ) {
        std::string opToken = parser->consume_token().lexeme;
        auto right = parse_equality_expr(parser);

        auto binaryNode = std::make_unique<BinaryExprNode>(
            opToken,
            std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
        );

        if (right && right->position) {
            pos->col[1] = right->position->col[1];
            pos->pos[1] = right->position->pos[1];
        }

        binaryNode->position = std::move(pos);
        expr = std::move(binaryNode);

        line = parser->current_token().line;
        column[0] = parser->current_token().column_start; column[1] = parser->current_token().column_end;
        position[0] = parser->current_token().position_start; position[1] = parser->current_token().position_end;
        pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    }

    return expr;
}