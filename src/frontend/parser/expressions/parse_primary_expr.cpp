#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/parser/expressions/parse_arguments_list.hpp"
#include "frontend/parser/expressions/parse_array_map_expr.hpp"
#include "frontend/parser/expressions/parse_vector_expr.hpp"
#include "frontend/parser/expressions/parse_boolean_literal.hpp"
std::unique_ptr<Node> parse_primary_expr(Parser* parser) {
    TokenType type = parser->current_token().type;

    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    std::unique_ptr<Node> expr;
    switch (type) {
        case TokenType::NUMBER: {
            Token numToken = parser->consume_token();
            auto node = std::make_unique<NumericLiteralNode>(numToken.lexeme);
            node->position = std::move(pos);
            expr = std::move(node);
            break;
        }
        case TokenType::IDENTIFIER: {
            Token idToken = parser->consume_token();
            auto node = std::make_unique<IdentifierNode>(idToken.lexeme);
            node->position = std::move(pos);
            expr = std::move(node);
            break;
        }
        case TokenType::STRING: {
            Token strToken = parser->consume_token();
            auto node = std::make_unique<StringLiteralNode>(strToken.lexeme);
            node->position = std::move(pos);
            expr = std::move(node);
            break;
        }
        case TokenType::OPAREN: {
            parser->consume_token();
            
            // Check if it's a tuple
            if (parser->current_token().type == TokenType::CPAREN) {
                // Empty tuple: ()
                parser->expect(TokenType::CPAREN, "Expected ')'.");
                auto tuple_node = std::make_unique<TupleExprNode>(std::vector<std::unique_ptr<Expr>>{});
                tuple_node->position = std::move(pos);
                expr = std::move(tuple_node);
                break;
            }
            
            // Try to parse first expression
            auto first_expr = parse_logical_expr(parser);
            
            // Check if there's a comma, indicating a tuple
            if (parser->current_token().type == TokenType::COMMA) {
                // It's a tuple with at least one element
                std::vector<std::unique_ptr<Expr>> elements;
                elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(first_expr.release())));
                
                // Parse remaining elements
                while (parser->current_token().type == TokenType::COMMA) {
                    parser->consume_token(); // consume ','
                    auto element = parse_logical_expr(parser);
                    elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(element.release())));
                }
                
                parser->expect(TokenType::CPAREN, "Expected ')'.");
                
                auto tuple_node = std::make_unique<TupleExprNode>(std::move(elements));
                tuple_node->position = std::move(pos);
                expr = std::move(tuple_node);
                break;
            }
            
            // It's a parenthesized expression
            parser->expect(TokenType::CPAREN, "Expected ')'.");
            if (first_expr && first_expr->position) {
                pos->col[1] = first_expr->position->col[1];
                pos->pos[1] = first_expr->position->pos[1];
            }
            first_expr->position = std::move(pos);
            expr = std::move(first_expr);
            break;
        }
        case TokenType::OBRACKET: {
            auto vector_node = parse_vector_expr(parser);
            if (vector_node && vector_node->position) {
                pos->col[1] = vector_node->position->col[1];
                pos->pos[1] = vector_node->position->pos[1];
            }
            vector_node->position = std::move(pos);
            expr = std::move(vector_node);
            break;
        }
        case TokenType::OBRACE: {
            auto array_map_node = parse_array_map_expr(parser);
            if (array_map_node && array_map_node->position) {
                pos->col[1] = array_map_node->position->col[1];
                pos->pos[1] = array_map_node->position->pos[1];
            }
            array_map_node->position = std::move(pos);
            expr = std::move(array_map_node);
            break;
        }
        case TokenType::TRUE:
        case TokenType::FALSE:
            expr = parse_boolean_literal(parser);
            break;
        default:
            parser->error("Unexpected token in primary expression: '" + parser->current_token().lexeme + "'");
            return nullptr;
    }
    return expr;
}