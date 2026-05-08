#include "frontend/parser/statements/parse_declaration_stmt.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_or_expr.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/expressions/parse_access_expr.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"
#include "frontend/parser/expressions/parse_unary_expr.hpp"
#include "frontend/parser/expressions/parse_additive_expr.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/parser/expressions/parse_equality_expr.hpp"
#include "frontend/parser/expressions/parse_relational_expr.hpp"
#include "frontend/parser/expressions/parse_assignment_expr.hpp"
#include "frontend/parser/expressions/parse_vector_expr.hpp"
#include "frontend/parser/expressions/parse_array_map_expr.hpp"
#include "frontend/ast/expressions/assignment_expr_node.hpp"

std::unique_ptr<Node> parse_expr(Parser* parser) {
    if (parser->current_token().type == TokenType::OBRACKET) {
        return parse_vector_expr(parser);
    }
    
    if (parser->current_token().type == TokenType::OBRACE) {
        return parse_array_map_expr(parser);
    }

    if (parser->current_token().type == TokenType::MINUS ||
        parser->current_token().type == TokenType::NOT ||
        parser->current_token().type == TokenType::INCREMENT ||
        parser->current_token().type == TokenType::DECREMENT ||
        parser->next_token().type == TokenType::INCREMENT ||
        parser->next_token().type == TokenType::DECREMENT
    ) {
        return parse_unary_expr(parser);
    }
    
    // If the next token starts a postfix chain (call, member, access),
    // parse a primary and delegate to the unified call/member/access parser.
    if (
        parser->next_token().type == TokenType::OPAREN ||
        parser->next_token().type == TokenType::OBRACKET ||
        parser->next_token().type == TokenType::DOT
    ) {
        std::unique_ptr<Node> expr = parse_primary_expr(parser);
        expr = parse_call_member_expr(parser, std::move(expr));

        // Handle assignment to member/call result: obj.field = value
        if (parser->current_token().type == TokenType::ASSIGNMENT ||
            parser->current_token().type == TokenType::PLUS_ASSIGN ||
            parser->current_token().type == TokenType::MINUS_ASSIGN ||
            parser->current_token().type == TokenType::MUL_ASSIGN ||
            parser->current_token().type == TokenType::DIV_ASSIGN ||
            parser->current_token().type == TokenType::INTEGER_DIV_ASSIGN ||
            parser->current_token().type == TokenType::POWER_ASSIGN ||
            parser->current_token().type == TokenType::MOD_ASSIGN ||
            parser->current_token().type == TokenType::BITWISE_AND_ASSIGN ||
            parser->current_token().type == TokenType::BITWISE_OR_ASSIGN ||
            parser->current_token().type == TokenType::BITWISE_XOR_ASSIGN ||
            parser->current_token().type == TokenType::LEFT_SHIFT_ASSIGN ||
            parser->current_token().type == TokenType::RIGHT_SHIFT_ASSIGN ||
            parser->current_token().type == TokenType::AT_ASSIGN) {

            std::string assign_op = parser->consume_token().lexeme;
            auto value = parse_logical_expr(parser);
            parser->expect(TokenType::SEMICOLON, "Expected ';'.");

            return std::make_unique<AssignmentExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
                assign_op,
                std::unique_ptr<Expr>(static_cast<Expr*>(value.release()))
            );
        }

        return try_parse_or(parser, std::move(expr));
    }

    if (parser->next_token().type == TokenType::COLON) {
        return parse_declaration_stmt(parser, false);
    }

    switch (parser->next_token().type) {
        case TokenType::AND:
        case TokenType::OR:
        case TokenType::EQUALS:
        case TokenType::DIFFERENT:
        case TokenType::LESS_THAN_EQUALS:
        case TokenType::GREATER_THAN_EQUALS:
        case TokenType::LT:
        case TokenType::GT:
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::MUL:
        case TokenType::DIV:
        case TokenType::MOD:
        case TokenType::POWER:
        case TokenType::INTEGER_DIV: {
            return parse_logical_expr(parser);
        }
        default: return parse_assignment_expr(parser);
    }
}