#include "frontend/parser/statements/parse_defer_stmt.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/ast/statements/defer_stmt_node.hpp"

// Parse: `defer { body }` ou `defer stmt;`
// remaining_body é preenchido pelo parse_body() do bloco pai.
std::unique_ptr<Node> parse_defer_stmt(Parser* parser) {
    parser->consume_token(); // consume 'defer'

    std::vector<std::unique_ptr<Node>> body;

    if (parser->current_token().type == TokenType::OBRACE) {
        // Forma com bloco: defer { ... }
        parser->consume_token(); // consume '{'
        while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
            auto s = parse_stmt(parser);
            if (s) body.push_back(std::move(s));
        }
        parser->expect(TokenType::CBRACE, "Expected '}' after defer body");
    } else {
        // Forma sem bloco: defer expr;
        auto s = parse_stmt(parser);
        if (s) body.push_back(std::move(s));
    }

    return std::make_unique<DeferStmtNode>(std::move(body));
}
