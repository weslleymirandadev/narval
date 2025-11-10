#include "frontend/parser/expressions/parse_access_expr.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"

std::unique_ptr<Node> parse_access_expr(Parser* parser, std::unique_ptr<Node> expr) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    size_t line_array = parser->current_token().line;
    size_t column_array[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position_array[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos_array = std::make_unique<PositionData>(line_array, column_array[0], column_array[1], position_array[0], position_array[1]);

   if (parser->current_token().type == TokenType::OBRACKET) {
        parser->consume_token(); 

        if (parser->current_token().type == TokenType::CBRACKET) {
            parser->error("Expected array index.");
            return nullptr;
        }

        auto index = parse_expr(parser);

        parser->expect(TokenType::CBRACKET, "Expected ']'.");
        
        if (index && index->position) {
            pos_array->col[1] = parser->current_token().column_end - 1;
            pos_array->pos[1] = parser->current_token().position_end - 1;
        }

        if (parser->current_token().type == TokenType::ASSIGNMENT) {
            parser->consume_token(); 

            auto value = parse_expr(parser);

            std::unique_ptr<Node> access_node = std::make_unique<AccessExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
                std::unique_ptr<Expr>(static_cast<Expr*>(index.release()))
            );

            if (value && value->position) {
                pos->col[1] = parser->current_token().column_end - 1;
                pos->pos[1] = parser->current_token().position_end - 1;
            }

            std::unique_ptr<Node> assign_node = std::make_unique<AssignmentExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(access_node.release())),
                std::unique_ptr<Expr>(static_cast<Expr*>(value.release()))
            );

            assign_node->position = std::move(pos);
            return assign_node;
        } else if (
            parser->current_token().type == TokenType::OPAREN ||
            parser->current_token().type == TokenType::OBRACKET ||
            parser->current_token().type == TokenType::DOT
        ) {
            std::unique_ptr<Node> access_node = std::make_unique<AccessExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
                std::unique_ptr<Expr>(static_cast<Expr*>(index.release()))
            );

            if (index && index->position) {
                pos->col[1] = parser->current_token().column_end - 1;
                pos->pos[1] = parser->current_token().position_end - 1;
            }

            access_node->position = std::move(pos);
            return parse_call_member_expr(parser, std::move(access_node));
        } else {
            std::unique_ptr<Node> access_node = std::make_unique<AccessExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(expr.release())),
                std::unique_ptr<Expr>(static_cast<Expr*>(index.release()))
            );

            if (index && index->position) {
                pos->col[1] = parser->current_token().column_end - 1;
                pos->pos[1] = parser->current_token().position_end - 1;
            }

            access_node->position = std::move(pos);
            return access_node;
        }

   } else {
        return expr;
   }
}