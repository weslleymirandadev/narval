#include "frontend/parser/statements/parse_attribute_stmt.hpp"
#include "frontend/ast/statements/attribute_stmt_node.hpp"
#include <iostream>

std::unique_ptr<Node> parse_attribute_stmt(Parser* parser) {
    Token at_tok = parser->consume_token();

    parser->expect(TokenType::OBRACKET, "Expected '[' after '@' for attribute statement");

    auto node = std::make_unique<AttributeStmtNode>();

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACKET) {
        if (parser->current_token().type == TokenType::IDENTIFIER) {
            node->attrs.push_back(parser->current_token().lexeme);
            parser->consume_token();
        } else if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        } else {
            parser->error("Invalid attribute syntax (only simple identifiers allowed)");
            return nullptr;
        }
    }

    Token close_tok = parser->current_token();
    parser->expect(TokenType::CBRACKET, "Expected ']' to close attribute");
    node->position = std::make_unique<PositionData>(
        at_tok.line,
        at_tok.column_start,
        close_tok.column_end,
        at_tok.position_start,
        close_tok.position_end,
        at_tok.filename
    );

    return node;
}
