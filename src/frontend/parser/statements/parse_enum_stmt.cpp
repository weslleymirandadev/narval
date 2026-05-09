#include "frontend/parser/statements/parse_enum_stmt.hpp"

std::unique_ptr<Node> parse_enum_stmt(Parser* parser) {
    parser->consume_token(); // consume ENUM

    std::string enum_name = parser->current_token().lexeme;
    parser->consume_token(); // consume enum name

    auto enum_node = std::make_unique<EnumStmtNode>(enum_name);

    parser->expect(TokenType::OBRACE, "Expected '{' after enum name");

    int next_value = 0;

    while (parser->current_token().type != TokenType::CBRACE &&
           parser->current_token().type != TokenType::EOF_TOKEN) {

        if (parser->current_token().type != TokenType::IDENTIFIER) {
            parser->error("Expected identifier in enum body");
            break;
        }

        std::string variant_name = parser->current_token().lexeme;
        parser->consume_token();

        EnumVariant variant(variant_name);

        if (parser->current_token().type == TokenType::ASSIGNMENT) {
            parser->consume_token(); // consume =

            bool negative = false;
            if (parser->current_token().type == TokenType::MINUS) {
                negative = true;
                parser->consume_token();
            }

            if (parser->current_token().type != TokenType::NUMBER) {
                parser->error("Expected integer literal after '=' in enum variant");
                break;
            }

            int val = std::stoi(parser->current_token().lexeme);
            if (negative) val = -val;
            parser->consume_token();

            variant.has_explicit_value = true;
            variant.explicit_value = val;
            next_value = val + 1;
        } else {
            variant.has_explicit_value = false;
            variant.explicit_value = next_value;
            next_value++;
        }

        enum_node->variants.push_back(variant);

        if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        }
    }

    parser->expect(TokenType::CBRACE, "Expected '}' after enum body");
    return std::move(enum_node);
}
