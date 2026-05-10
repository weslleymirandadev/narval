#include "frontend/parser/statements/parse_defer_error_stmt.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/ast/statements/defer_error_stmt_node.hpp"

// Parse: `defer error { handler_body }`
// O remaining_body é preenchido pelo parse_body() do bloco pai.
std::unique_ptr<Node> parse_defer_error_stmt(Parser* parser) {
    parser->consume_token(); // consume 'defer'

    // Espera identificador 'error'
    if (parser->current_token().type != TokenType::IDENTIFIER ||
        parser->current_token().lexeme != "error") {
        parser->error("Expected 'error' after 'defer'");
        return nullptr;
    }
    parser->consume_token(); // consume 'error'

    if (parser->current_token().type != TokenType::OBRACE) {
        parser->error("Expected '{' after 'defer error'");
        return nullptr;
    }
    parser->consume_token(); // consume '{'

    std::vector<std::unique_ptr<Node>> handler;
    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACE) {
        auto s = parse_stmt(parser);
        if (s) handler.push_back(std::move(s));
    }
    parser->expect(TokenType::CBRACE, "Expected '}' after defer error handler");

    return std::make_unique<DeferErrorStmtNode>(std::move(handler));
}
