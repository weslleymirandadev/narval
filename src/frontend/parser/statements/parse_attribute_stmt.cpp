#include "frontend/parser/statements/parse_attribute_stmt.hpp"
#include "frontend/ast/statements/attribute_stmt_node.hpp"
#include <iostream>

std::unique_ptr<Node> parse_attribute_stmt(Parser* parser) {
    Token at_tok = parser->consume_token();

    parser->expect(TokenType::OBRACKET, "Expected '[' after '@' for attribute statement");

    auto node = std::make_unique<AttributeStmtNode>();

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACKET) {
        auto cur = parser->current_token();

        if (cur.type == TokenType::IDENTIFIER) {
            AttrEntry entry;
            entry.name = cur.lexeme;
            parser->consume_token();

            // Atributo com argumentos: @[abi("sysv64")]
            if (parser->current_token().type == TokenType::OPAREN) {
                parser->consume_token(); // consume '('
                while (parser->not_eof() && parser->current_token().type != TokenType::CPAREN) {
                    auto arg_tok = parser->current_token();
                    if (arg_tok.type == TokenType::STRING ||
                        arg_tok.type == TokenType::NUMBER ||
                        arg_tok.type == TokenType::IDENTIFIER ||
                        arg_tok.type == TokenType::TRUE ||
                        arg_tok.type == TokenType::FALSE) {
                        entry.args.push_back({arg_tok.lexeme});
                        parser->consume_token();
                    } else {
                        parser->error("Expected string, number or identifier as attribute argument");
                        return nullptr;
                    }
                    if (parser->current_token().type == TokenType::COMMA)
                        parser->consume_token();
                }
                parser->expect(TokenType::CPAREN, "Expected ')' to close attribute arguments");
            }

            node->attrs.push_back(entry.name);
            node->entries.push_back(std::move(entry));

        } else if (cur.type == TokenType::COMMA) {
            parser->consume_token();
        } else {
            parser->error("Invalid attribute syntax");
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
