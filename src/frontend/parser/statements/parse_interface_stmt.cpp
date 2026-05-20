#include "frontend/parser/statements/parse_interface_stmt.hpp"
#include "frontend/parser/expressions/parse_type.hpp"

std::unique_ptr<Node> parse_interface_stmt(Parser* parser) {
    parser->consume_token(); // consume INTERFACE

    std::string iface_name = parser->current_token().lexeme;
    parser->consume_token(); // consume name

    auto iface_node = std::make_unique<InterfaceStmtNode>(iface_name);

    // extends Interface1, Interface2
    if (parser->current_token().type == TokenType::EXTENDS) {
        parser->consume_token();
        while (parser->current_token().type == TokenType::IDENTIFIER) {
            iface_node->parent_interfaces.push_back(parser->current_token().lexeme);
            parser->consume_token();
            if (parser->current_token().type == TokenType::COMMA)
                parser->consume_token();
            else
                break;
        }
    }

    parser->expect(TokenType::OBRACE, "Expected '{' after interface name");

    while (parser->current_token().type != TokenType::CBRACE &&
           parser->current_token().type != TokenType::EOF_TOKEN) {

        // Supports: methodName(params): returnType; or def methodName(params): returnType;
        if (parser->current_token().type == TokenType::DEF)
            parser->consume_token(); // consume optional `def`

        if (parser->current_token().type != TokenType::IDENTIFIER &&
            parser->current_token().type != TokenType::NEW) {
            parser->error("Expected method name in interface body");
            break;
        }

        InterfaceMethodSig sig;
        sig.name = parser->current_token().lexeme;
        parser->consume_token(); // consume method name

        parser->expect(TokenType::OPAREN, "Expected '(' after method name in interface");
        while (parser->current_token().type != TokenType::CPAREN &&
               parser->current_token().type != TokenType::EOF_TOKEN) {
            if (parser->current_token().type == TokenType::IDENTIFIER) {
                std::string pname = parser->current_token().lexeme;
                parser->consume_token();
                std::string ptype = "automatic";
                if (parser->current_token().type == TokenType::COLON) {
                    parser->consume_token();
                    ptype = parse_type(parser);
                }
                sig.params.push_back({pname, ptype});
                if (parser->current_token().type == TokenType::COMMA)
                    parser->consume_token();
            } else {
                break;
            }
        }
        parser->expect(TokenType::CPAREN, "Expected ')' after parameters");

        sig.return_type = "None";
        if (parser->current_token().type == TokenType::COLON) {
            parser->consume_token();
            sig.return_type = parse_type(parser);
        }

        parser->expect(TokenType::SEMICOLON, "Expected ';' after interface method signature");

        iface_node->methods.push_back(std::move(sig));
    }

    parser->expect(TokenType::CBRACE, "Expected '}' after interface body");
    return std::move(iface_node);
}
