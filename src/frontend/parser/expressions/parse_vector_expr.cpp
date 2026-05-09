#include "frontend/parser/expressions/parse_vector_expr.hpp"
#include "frontend/parser/expressions/parse_array_map_expr.hpp"
#include "frontend/parser/expressions/parse_list_comp_expr.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/ast/expressions/conditional_expr_node.hpp"
#include "frontend/ast/expressions/binary_expr_node.hpp"
#include "frontend/ast/expressions/list_comp_node.hpp"
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
        
        // Se IF vier antes do FOR, pode ser ternário no elemento: [x if cond else y for ...]
        if (parser->current_token().type == TokenType::IF) {
            parser->consume_token();
            auto cond = parse_logical_expr(parser);
            if (parser->current_token().type == TokenType::ELSE) {
                // Ternário completo: x if cond else y — pode ser elemento de list comp
                parser->consume_token();
                auto false_val = parse_logical_expr(parser);
                auto cond_node = std::make_unique<ConditionalExprNode>(
                    std::unique_ptr<Expr>(static_cast<Expr*>(test_expr.release())),
                    std::unique_ptr<Expr>(static_cast<Expr*>(cond.release())),
                    std::unique_ptr<Expr>(static_cast<Expr*>(false_val.release()))
                );
                test_expr = std::unique_ptr<Node>(cond_node.release());
            } else {
                // Sem else: tratar IF como filter — colocar o IF de volta reconstruindo um ListComp
                // Re-use test_expr como elt, e a condition como if_cond
                // Mas o FOR ainda tem que vir depois...
                // Por ora, avançar esperando FOR
                auto elt = std::unique_ptr<Expr>(static_cast<Expr*>(test_expr.release()));
                auto cond_expr = std::unique_ptr<Expr>(static_cast<Expr*>(cond.release()));
                // Construir ListCompNode diretamente
                auto lc_node = parse_list_comp_expr(parser, std::move(elt));
                // Precisamos injetar o if_cond... mas o parser já consumiu o IF e a cond
                // Vamos criar um ListCompNode com a cond que temos
                // Isso exige acesso ao interior... vamos usar abordagem diferente:
                // Criar um node temporário e inserir a cond
                auto* lc = static_cast<ListCompNode*>(lc_node.get());
                if (!lc->if_cond) {
                    lc->if_cond = std::move(cond_expr);
                } else {
                    auto and_node = std::make_unique<BinaryExprNode>(
                        "&&", std::move(lc->if_cond), std::move(cond_expr));
                    lc->if_cond = std::move(and_node);
                }
                // List comp is the whole expression — return directly
                parser->expect(TokenType::CBRACKET, "Expected ']'.");
                lc_node->position = std::move(pos);
                return lc_node;
            }
        }

        if (parser->current_token().type == TokenType::FOR) {
            auto elt = std::unique_ptr<Expr>(static_cast<Expr*>(test_expr.release()));
            auto node = parse_list_comp_expr(parser, std::move(elt));
            // List comp is the whole expression — expect closing ] and return directly
            parser->expect(TokenType::CBRACKET, "Expected ']'.");
            node->position = std::move(pos);
            return node;
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