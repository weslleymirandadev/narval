#include "frontend/parser/expressions/parse_member_expr.hpp"
#include "frontend/parser/expressions/parse_primary_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"

std::unique_ptr<Node> parse_member_expr(Parser* parser) {
    size_t line = parser->current_token().line;
    size_t column[2] = { parser->current_token().column_start, parser->current_token().column_end };
    size_t position[2] = { parser->current_token().position_start, parser->current_token().position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);

    auto object = parse_primary_expr(parser);

    while (parser->current_token().type == TokenType::DOT) {
        parser->consume_token();

        std::unique_ptr<Node> property; 

        size_t line_property = parser->current_token().line;
        size_t column_property[2] = { parser->current_token().column_start, parser->current_token().column_end };
        size_t position_property[2] = { parser->current_token().position_start, parser->current_token().position_end };
        std::unique_ptr<PositionData> pos_property = std::make_unique<PositionData>(line_property, column_property[0], column_property[1], position_property[0], position_property[1]);

        // Only allow IDENTIFIER or integer NUMBER as a member property
        Token current = parser->current_token();
        if (current.type == TokenType::IDENTIFIER) {
            Token idTok = parser->consume_token();
            auto idNode = std::make_unique<IdentifierNode>(idTok.lexeme);
            idNode->position = std::make_unique<PositionData>(line_property, column_property[0], column_property[1], position_property[0], position_property[1]);
            property = std::move(idNode);
        } else if (current.type == TokenType::NUMBER) {
            Token numTok = parser->consume_token();
            const std::string &lex = numTok.lexeme;
            auto numNode = std::make_unique<NumericLiteralNode>(numTok.lexeme);
            numNode->position = std::make_unique<PositionData>(line_property, column_property[0], column_property[1], position_property[0], position_property[1]);
            property = std::move(numNode);
        } else {
            parser->error("Expected identifier or integer literal after '.'");
            return object;
        }

        if (property && property->position) {
            pos_property->col[1] = property->position->col[1];
            pos_property->pos[1] = property->position->pos[1];
        }

        property->position = std::move(pos_property);

        auto member = std::make_unique<MemberExprNode>(
            std::unique_ptr<Expr>(static_cast<Expr*>(object.release())),
            std::unique_ptr<Expr>(static_cast<Expr*>(property.release()))
        );

        if (member && member->position) {
            pos->col[1] = member->position->col[1];
            pos->pos[1] = member->position->pos[1];
        }

        member->position = std::move(pos);
        object = std::move(member);
    }

    return object;
}