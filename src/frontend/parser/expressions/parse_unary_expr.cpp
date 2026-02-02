#include "frontend/parser/expressions/parse_additive_expr.hpp"
#include "frontend/parser/expressions/parse_unary_expr.hpp"
#include "frontend/parser/expressions/parse_postfix_expr.hpp"

std::unique_ptr<Node> parse_unary_expr(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    std::unique_ptr<Node> expr;

    switch (parser->current_token().type) {
        case TokenType::MINUS: {
            parser->consume_token();

            auto operand = parse_unary_expr(parser);

            auto unary_node = std::make_unique<UnaryMinusExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(operand.release()))
            );

            if (operand && operand->position) {
                pos->col[1] = operand->position->col[1];
                pos->pos[1] = operand->position->pos[1];
            }

            unary_node->position = std::move(pos);

            expr = std::move(unary_node);
            break;
        }
        case TokenType::NOT: {
            parser->consume_token();

            auto operand = parse_unary_expr(parser);

            auto unary_node = std::make_unique<LogicalNotExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(operand.release()))
            );

            if (operand && operand->position) {
                pos->col[1] = operand->position->col[1];
                pos->pos[1] = operand->position->pos[1];
            }

            unary_node->position = std::move(pos);

            expr = std::move(unary_node);
            break;
        }
        case TokenType::INCREMENT: {
            parser->consume_token();

            auto operand = parse_unary_expr(parser);

            auto unary_node = std::make_unique<IncrementExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(operand.release()))
            );

            if (operand && operand->position) {
                pos->col[1] = operand->position->col[1];
                pos->pos[1] = operand->position->pos[1];
            }

            unary_node->position = std::move(pos);

            expr = std::move(unary_node);
            break;
        }
        case TokenType::DECREMENT: {
            parser->consume_token();

            auto operand = parse_unary_expr(parser);

            auto unary_node = std::make_unique<DecrementExprNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(operand.release()))
            );

            if (operand && operand->position) {
                pos->col[1] = operand->position->col[1];
                pos->pos[1] = operand->position->pos[1];
            }

            unary_node->position = std::move(pos);

            expr = std::move(unary_node);
            break;
        }
        default: {
            auto exp = parse_additive_expr(parser);

            if (
                parser->current_token().type == TokenType::INCREMENT ||
                parser->current_token().type == TokenType::DECREMENT
            ) {
                return parse_postfix_expr(parser, std::move(exp));
            }
                
            return exp;
            break;
        }
    }

    // Verificar se há operadores postfix após operadores unários
    // Exemplo: -lista[0]++ ou ++lista[0]--
    if (
        parser->current_token().type == TokenType::INCREMENT ||
        parser->current_token().type == TokenType::DECREMENT
    ) {
        return parse_postfix_expr(parser, std::move(expr));
    }

    return expr;
}