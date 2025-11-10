#include "frontend/parser/statements/parse_locked_stmt.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/expressions/parse_access_expr.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"
#include "frontend/parser/expressions/parse_unary_expr.hpp"
#include "frontend/parser/expressions/parse_additive_expr.hpp"
#include "frontend/parser/expressions/parse_assignment_expr.hpp"
#include "frontend/parser/expressions/parse_vector_expr.hpp"
#include "frontend/parser/expressions/parse_array_map_expr.hpp"

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
        expr =  parse_call_member_expr(parser, std::move(expr));
        return expr;
    }

    if (parser->next_token().type == TokenType::COLON) {
        return parse_locked_stmt(parser, false);
    }

    switch (parser->next_token().type) {
        case TokenType::PLUS:
        case TokenType::MINUS:
        case TokenType::MUL:
        case TokenType::DIV:
        case TokenType::MOD:
        case TokenType::POWER:
        case TokenType::INTEGER_DIV: {
            return parse_additive_expr(parser);
        }
        default: return parse_assignment_expr(parser);
    }
}