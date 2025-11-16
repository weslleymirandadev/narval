#include "frontend/parser/expressions/parse_access_expr.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_power_expr.hpp"
#include "frontend/parser/expressions/parse_multiplicative_expr.hpp"
#include "frontend/parser/expressions/parse_additive_expr.hpp"
#include "frontend/parser/expressions/parse_logical_not_expr.hpp"
#include "frontend/parser/expressions/parse_relational_expr.hpp"
#include "frontend/parser/expressions/parse_equality_expr.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"

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
                assign,
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

            std::unique_ptr<Node> left = std::move(access_node);

            size_t bline = parser->current_token().line;
            size_t bcolumn[2] = { parser->current_token().column_start, parser->current_token().column_end };
            size_t bposition[2] = { parser->current_token().position_start, parser->current_token().position_end };
            std::unique_ptr<PositionData> bpos = std::make_unique<PositionData>(bline, bcolumn[0], bcolumn[1], bposition[0], bposition[1]);

            while (parser->current_token().type == TokenType::POWER) {
                std::string opToken = parser->consume_token().lexeme;
                auto right = parse_call_member_expr(parser, nullptr);

                auto bin = std::make_unique<BinaryExprNode>(
                    opToken,
                    std::unique_ptr<Expr>(static_cast<Expr*>(left.release())),
                    std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
                );

                if (right && right->position) {
                    bpos->col[1] = right->position->col[1];
                    bpos->pos[1] = right->position->pos[1];
                }
                bin->position = std::move(bpos);
                left = std::move(bin);

                bline = parser->current_token().line;
                bcolumn[0] = parser->current_token().column_start; bcolumn[1] = parser->current_token().column_end;
                bposition[0] = parser->current_token().position_start; bposition[1] = parser->current_token().position_end;
                bpos = std::make_unique<PositionData>(bline, bcolumn[0], bcolumn[1], bposition[0], bposition[1]);
            }

            while (
                parser->current_token().type == TokenType::MUL ||
                parser->current_token().type == TokenType::DIV ||
                parser->current_token().type == TokenType::MOD ||
                parser->current_token().type == TokenType::INTEGER_DIV
            ) {
                std::string opToken = parser->consume_token().lexeme;
                auto right = parse_power_expr(parser);

                auto bin = std::make_unique<BinaryExprNode>(
                    opToken,
                    std::unique_ptr<Expr>(static_cast<Expr*>(left.release())),
                    std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
                );

                if (right && right->position) {
                    bpos->col[1] = right->position->col[1];
                    bpos->pos[1] = right->position->pos[1];
                }
                bin->position = std::move(bpos);
                left = std::move(bin);

                bline = parser->current_token().line;
                bcolumn[0] = parser->current_token().column_start; bcolumn[1] = parser->current_token().column_end;
                bposition[0] = parser->current_token().position_start; bposition[1] = parser->current_token().position_end;
                bpos = std::make_unique<PositionData>(bline, bcolumn[0], bcolumn[1], bposition[0], bposition[1]);
            }

            while (
                parser->current_token().type == TokenType::PLUS ||
                parser->current_token().type == TokenType::MINUS
            ) {
                std::string opToken = parser->consume_token().lexeme;
                auto right = parse_multiplicative_expr(parser);

                auto bin = std::make_unique<BinaryExprNode>(
                    opToken,
                    std::unique_ptr<Expr>(static_cast<Expr*>(left.release())),
                    std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
                );

                if (right && right->position) {
                    bpos->col[1] = right->position->col[1];
                    bpos->pos[1] = right->position->pos[1];
                }
                bin->position = std::move(bpos);
                left = std::move(bin);

                bline = parser->current_token().line;
                bcolumn[0] = parser->current_token().column_start; bcolumn[1] = parser->current_token().column_end;
                bposition[0] = parser->current_token().position_start; bposition[1] = parser->current_token().position_end;
                bpos = std::make_unique<PositionData>(bline, bcolumn[0], bcolumn[1], bposition[0], bposition[1]);
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

                auto bin = std::make_unique<BinaryExprNode>(
                    opToken,
                    std::unique_ptr<Expr>(static_cast<Expr*>(left.release())),
                    std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
                );

                if (right && right->position) {
                    bpos->col[1] = right->position->col[1];
                    bpos->pos[1] = right->position->pos[1];
                }
                bin->position = std::move(bpos);
                left = std::move(bin);

                bline = parser->current_token().line;
                bcolumn[0] = parser->current_token().column_start; bcolumn[1] = parser->current_token().column_end;
                bposition[0] = parser->current_token().position_start; bposition[1] = parser->current_token().position_end;
                bpos = std::make_unique<PositionData>(bline, bcolumn[0], bcolumn[1], bposition[0], bposition[1]);
            }

            // Equality: ==, !=
            while (
                parser->current_token().type == TokenType::EQUALS ||
                parser->current_token().type == TokenType::DIFFERENT
            ) {
                std::string opToken = parser->consume_token().lexeme;
                auto right = parse_relational_expr(parser);

                auto bin = std::make_unique<BinaryExprNode>(
                    opToken,
                    std::unique_ptr<Expr>(static_cast<Expr*>(left.release())),
                    std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
                );

                if (right && right->position) {
                    bpos->col[1] = right->position->col[1];
                    bpos->pos[1] = right->position->pos[1];
                }
                bin->position = std::move(bpos);
                left = std::move(bin);

                bline = parser->current_token().line;
                bcolumn[0] = parser->current_token().column_start; bcolumn[1] = parser->current_token().column_end;
                bposition[0] = parser->current_token().position_start; bposition[1] = parser->current_token().position_end;
                bpos = std::make_unique<PositionData>(bline, bcolumn[0], bcolumn[1], bposition[0], bposition[1]);
            }

            // Logical: &&, ||
            while (
                parser->current_token().type == TokenType::AND ||
                parser->current_token().type == TokenType::OR
            ) {
                std::string opToken = parser->consume_token().lexeme;
                auto right = parse_equality_expr(parser);

                auto bin = std::make_unique<BinaryExprNode>(
                    opToken,
                    std::unique_ptr<Expr>(static_cast<Expr*>(left.release())),
                    std::unique_ptr<Expr>(static_cast<Expr*>(right.release()))
                );

                if (right && right->position) {
                    bpos->col[1] = right->position->col[1];
                    bpos->pos[1] = right->position->pos[1];
                }
                bin->position = std::move(bpos);
                left = std::move(bin);

                bline = parser->current_token().line;
                bcolumn[0] = parser->current_token().column_start; bcolumn[1] = parser->current_token().column_end;
                bposition[0] = parser->current_token().position_start; bposition[1] = parser->current_token().position_end;
                bpos = std::make_unique<PositionData>(bline, bcolumn[0], bcolumn[1], bposition[0], bposition[1]);
            }

            return left;
        }

   } else {
        return expr;
   }
}