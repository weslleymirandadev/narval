#include "frontend/parser/expressions/parse_array_map_expr.hpp"
#include "frontend/parser/expressions/parse_additive_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"

std::unique_ptr<Node> parse_array_map_expr(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    if (parser->current_token().type != TokenType::OBRACE) {
        return parse_additive_expr(parser);
    }

    // consume '{'
    parser->consume_token();

    // Empty braced literal: treat as empty array by default
    if (parser->current_token().type == TokenType::CBRACE) {
        parser->consume_token();
        auto array_node = std::make_unique<ArrayExprNode>(std::vector<std::unique_ptr<Expr>>{});
        if (array_node && array_node->position) {
            pos->col[1] = parser->current_token().column_end - 1;
            pos->pos[1] = parser->current_token().position_end - 1;
        }
        array_node->position = std::move(pos);
        return array_node;
    }

    // Parse a head expression to decide between map and array.
    // For keys, allow primary + call/member/access chain without consuming semicolons.
    auto head = parse_primary_expr(parser);
    head = parse_call_member_expr(parser, std::move(head));

    if (parser->current_token().type == TokenType::COLON) {
        // It's a map: first entry has 'head' as key expression.
        std::vector<std::unique_ptr<Expr>> properties;

        // Parse first key:value
        parser->consume_token(); // consume ':'
        auto first_value = parse_additive_expr(parser);
        properties.push_back(std::make_unique<KeyValueNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(head.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(first_value.release()))
        ));

        // Parse remaining entries
        while (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
            if (parser->current_token().type == TokenType::CBRACE) break; // trailing comma

            auto key_expr = parse_primary_expr(parser);
            key_expr = parse_call_member_expr(parser, std::move(key_expr));

            parser->expect(TokenType::COLON, "Expected ':'.");
            auto val_expr = parse_additive_expr(parser);

            properties.push_back(std::make_unique<KeyValueNode>(
                std::unique_ptr<Expr>(static_cast<Expr*>(key_expr.release())),
                std::unique_ptr<Expr>(static_cast<Expr*>(val_expr.release()))
            ));
        }

        parser->expect(TokenType::CBRACE, "Expected '}'.");

        auto map_node = std::make_unique<MapNode>(std::move(properties));
        map_node->position = std::move(pos);
        return map_node;
    } else {
        // It's an array: 'head' is the first element.
        std::vector<std::unique_ptr<Expr>> elements;
        elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(head.release())));

        while (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
            if (parser->current_token().type == TokenType::CBRACE) break; // trailing comma

            auto elem_node = parse_additive_expr(parser);
            elements.push_back(std::unique_ptr<Expr>(static_cast<Expr*>(elem_node.release())));
        }

        parser->expect(TokenType::CBRACE, "Expected '}'.");

        auto array_node = std::make_unique<ArrayExprNode>(std::move(elements));
        if (array_node && array_node->position) {
            pos->col[1] = parser->current_token().column_end - 1;
            pos->pos[1] = parser->current_token().position_end - 1;
        }
        array_node->position = std::move(pos);
        return array_node;
    }
}