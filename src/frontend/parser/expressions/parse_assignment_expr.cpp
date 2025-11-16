#include "frontend/parser/expressions/parse_array_map_expr.hpp"
#include "frontend/parser/expressions/parse_vector_expr.hpp"
#include "frontend/parser/expressions/parse_assignment_expr.hpp"
#include "frontend/parser/expressions/parse_type.hpp"
#include "frontend/parser/expressions/parse_conditional_expr.hpp"
#include "frontend/parser/statements/parse_locked_stmt.hpp"
#include <iostream>

std::unique_ptr<Node> parse_assignment_expr(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    
    if (parser->current_token().type == TokenType::LOCK) {
        parser->consume_token();
        return parse_locked_stmt(parser, true);
    }
    
    auto target = parse_array_map_expr(parser);
    bool declaration = false;
    std::string typ = "automatic";
    
    if (parser->current_token().type == TokenType::COLON) {
        typ = parse_type(parser);
        parser->expect(TokenType::ASSIGNMENT, "Expected regular '='.");
        declaration = true;
    }
    if (parser->current_token().type == TokenType::ASSIGNMENT ||
        parser->current_token().type == TokenType::PLUS_ASSIGN ||
        parser->current_token().type == TokenType::MINUS_ASSIGN ||
        parser->current_token().type == TokenType::MUL_ASSIGN ||
        parser->current_token().type == TokenType::DIV_ASSIGN ||
        parser->current_token().type == TokenType::INTEGER_DIV_ASSIGN ||
        parser->current_token().type == TokenType::POWER_ASSIGN ||
        parser->current_token().type == TokenType::MOD_ASSIGN
    ) {
        std::string assign = parser->consume_token().lexeme;

        std::unique_ptr<Node> value;

        switch (parser->current_token().type) {
            case TokenType::OBRACKET: {
                value = parse_vector_expr(parser);
            } break;
            case TokenType::OBRACE: {
                value = parse_array_map_expr(parser);
            } break;
            default: {
                value = parse_assignment_expr(parser);
            } break;
        }

        if (parser->current_token().type == TokenType::IF) {
            value = parse_conditional_expr(parser, std::move(value));
        }

        parser->expect(TokenType::SEMICOLON, "Expected ';'.");//vo ali comer alguma bele
        std::unique_ptr<Node> node;
        if (declaration) {
            node = std::make_unique<DeclarationStmtNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(target.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(value.release())),
            typ            
        );
        } else {
            node = std::make_unique<AssignmentExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(target.release())),
                assign,
                std::unique_ptr<Expr>(static_cast<Expr*>(value.release()))
            );
        }
        
        if (value && value->position) {
            pos->col[1] = value->position->col[1];
            pos->pos[1] = value->position->pos[1];
        }
        node->position = std::move(pos);
        return node;
    }
    
    if (target && !target->position) {
        target->position = std::move(pos);
    }
    return target;
}