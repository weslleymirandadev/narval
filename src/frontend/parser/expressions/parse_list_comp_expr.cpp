#include "frontend/parser/expressions/parse_list_comp_expr.hpp"
#include "frontend/parser/expressions/parse_logical_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_conditional_expr.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"

std::unique_ptr<Node> parse_list_comp_expr(Parser* parser, std::unique_ptr<Expr> pre_parsed_elt) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    std::unique_ptr<Expr> elt;
    if (pre_parsed_elt) {
        elt = std::move(pre_parsed_elt);
    } else {
        auto elt_node = parse_logical_expr(parser);
        elt = std::unique_ptr<Expr>(static_cast<Expr*>(elt_node.release()));
    }
    
    std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> generators;
    
    while (parser->current_token().type == TokenType::FOR) {
        parser->consume_token();
        
        std::unique_ptr<Expr> target;
        
        if (parser->current_token().type == TokenType::OPAREN) {
            parser->consume_token();
            
            std::vector<std::unique_ptr<Expr>> tuple_elements;
            
            auto first_id_node = parse_primary_expr(parser);
            if (first_id_node->kind == NodeType::Identifier) {
                auto first_id = std::unique_ptr<Expr>(static_cast<Expr*>(first_id_node.release()));
                tuple_elements.push_back(std::move(first_id));
            } else {
                parser->error("Expected identifier in destructuring target.");
            }
            
            while (parser->current_token().type == TokenType::COMMA) {
                parser->consume_token();
                
                auto id_node = parse_primary_expr(parser);
                if (id_node->kind == NodeType::Identifier) {
                    auto id = std::unique_ptr<Expr>(static_cast<Expr*>(id_node.release()));
                    tuple_elements.push_back(std::move(id));
                } else {
                    parser->error("Expected identifier after ',' in destructuring.");
                }
            }
            
            parser->expect(TokenType::CPAREN, "Expected ')' in destructuring target.");
            
            target = std::make_unique<TupleExprNode>(std::move(tuple_elements));
        } else {
            auto first_id_node = parse_primary_expr(parser);
            if (first_id_node->kind != NodeType::Identifier) {
                parser->error("Expected identifier in list comprehension target.");
            }
            
            auto first_id = std::unique_ptr<Expr>(static_cast<Expr*>(first_id_node.release()));
            
            if (parser->current_token().type == TokenType::COMMA) {
                std::vector<std::unique_ptr<Expr>> tuple_elements;
                tuple_elements.push_back(std::move(first_id));
                
                while (parser->current_token().type == TokenType::COMMA) {
                    parser->consume_token();
                    
                    auto next_id_node = parse_primary_expr(parser);
                    if (next_id_node->kind == NodeType::Identifier) {
                        auto next_id = std::unique_ptr<Expr>(static_cast<Expr*>(next_id_node.release()));
                        tuple_elements.push_back(std::move(next_id));
                    } else {
                        parser->error("Expected identifier after ','.");
                    }
                }
                
                target = std::make_unique<TupleExprNode>(std::move(tuple_elements));
            } else {
                target = std::move(first_id);
            }
        }
        
        if (parser->current_token().type == TokenType::COLON) {
            parser->consume_token();
        } else {
            parser->expect(TokenType::COLON, "Expected ':'.");
        }
        
        auto source_node = parse_expr(parser);
        auto source = std::unique_ptr<Expr>(static_cast<Expr*>(source_node.release()));
        
        generators.push_back(std::make_pair(
            std::move(target),
            std::move(source)
        ));
    }
    
    std::unique_ptr<Expr> if_cond = nullptr;
    std::vector<std::unique_ptr<Expr>> if_conditions;
    
    while (parser->current_token().type == TokenType::IF) {
        parser->consume_token();
        auto condition_node = parse_logical_expr(parser);
        auto condition = std::unique_ptr<Expr>(static_cast<Expr*>(condition_node.release()));
        if_conditions.push_back(std::move(condition));
    }
    
    if (!if_conditions.empty()) {
        if_cond = std::move(if_conditions[0]);
        for (size_t i = 1; i < if_conditions.size(); ++i) {
            auto and_node = std::make_unique<BinaryExprNode>(
                "&&",
                std::move(if_cond),
                std::move(if_conditions[i])
            );
            if_cond = std::move(and_node);
        }
    }
    
    std::unique_ptr<Expr> else_expr = nullptr;
    if (parser->current_token().type == TokenType::ELSE) {
        parser->consume_token();
        auto else_node = parse_expr(parser);
        
        if (parser->current_token().type == TokenType::IF) {
            else_node = parse_conditional_expr(parser, std::move(else_node));
        }
        
        else_expr = std::unique_ptr<Expr>(static_cast<Expr*>(else_node.release()));
    }
    
    if (else_expr && else_expr->position) {
        pos->col[1] = else_expr->position->col[1];
        pos->pos[1] = else_expr->position->pos[1];
    } else if (if_cond && if_cond->position) {
        pos->col[1] = if_cond->position->col[1];
        pos->pos[1] = if_cond->position->pos[1];
    } else if (!generators.empty() && generators.back().second && generators.back().second->position) {
        pos->col[1] = generators.back().second->position->col[1];
        pos->pos[1] = generators.back().second->position->pos[1];
    }
    
    auto list_comp_node = std::make_unique<ListCompNode>(
        std::move(elt),
        std::move(generators),
        std::move(if_cond),
        std::move(else_expr)
    );
    
    list_comp_node->position = std::move(pos);
    
    return list_comp_node;
}
