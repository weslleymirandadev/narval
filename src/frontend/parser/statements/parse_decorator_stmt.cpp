#include "frontend/parser/statements/parse_decorator_stmt.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"

std::unique_ptr<Node> parse_decorator_stmt(Parser* parser) {
    parser->consume_token();
    
    auto node = std::make_unique<DecoratorStmtNode>();
    
    if (parser->next_token().type == TokenType::OBRACKET) {
        parser->consume_token(); 

        while (parser->not_eof() && parser->current_token().type != TokenType::CBRACKET) {
            if (parser->current_token().type == TokenType::IDENTIFIER) {
                node->decorators.push_back(parser->current_token().lexeme);
                parser->consume_token();
            } else if (parser->current_token().type == TokenType::COMMA) {
                parser->consume_token();
            } else {
                parser->error("Invalid decorator syntax (only simple identifiers allowed)");
                return nullptr;
            }
        }

        parser->expect(TokenType::CBRACKET, "Expected ']' to close decorator");
    } else {       
        if (parser->current_token().type == TokenType::IDENTIFIER) {
            node->decorators.push_back(parser->current_token().lexeme);
            parser->consume_token();
        } else {
            parser->error("Expected identifier or '['after '@' for decorator");
            return nullptr;
        }
    }

    return node;
}
