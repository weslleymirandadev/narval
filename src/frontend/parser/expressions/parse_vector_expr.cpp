#include "frontend/parser/expressions/parse_vector_expr.hpp"
#include "frontend/parser/expressions/parse_array_map_expr.hpp"
#include "frontend/parser/expressions/parse_list_comp_expr.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include <iostream>

std::unique_ptr<Node> parse_vector_expr(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    
    if (parser->current_token().type != TokenType::OBRACKET) {
        return parse_array_map_expr(parser);
    }

    std::vector<std::unique_ptr<Expr>> elements;

    parser->consume_token();
    
    while (parser->current_token().type != TokenType::CBRACKET) {
        auto test_expr = parse_logical_expr(parser);
        
        if (parser->current_token().type == TokenType::FOR) {
            auto elt = std::unique_ptr<Expr>(static_cast<Expr*>(test_expr.release()));
            
            auto node = parse_list_comp_expr(parser, std::move(elt));
            auto expr = std::unique_ptr<Expr>(static_cast<Expr*>(node.release()));
            elements.push_back(std::move(expr));
        } else {
            auto expr = std::unique_ptr<Expr>(static_cast<Expr*>(test_expr.release()));
            elements.push_back(std::move(expr));
        }

        if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        }
    }

    parser->expect(TokenType::CBRACKET, "Expected ']'.");
    
    auto array_node = std::make_unique<VectorExprNode>(std::move(elements));

    if (array_node && array_node->position) {
        pos->col[1] = parser->current_token().column_end - 1;
        pos->pos[1] = parser->current_token().position_end - 1;
    }

    array_node->position = std::move(pos);

    return array_node;
}