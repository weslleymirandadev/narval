#include "frontend/parser/statements/parse_attribute_stmt.hpp"
#include "frontend/ast/statements/module_attr_node.hpp"

std::unique_ptr<Node> parse_attribute_stmt(Parser* parser) {
    // Only handle the case: [ IDENTIFIER ( , IDENTIFIER )* ]
    if (parser->current_token().type != TokenType::OBRACKET) return nullptr;

    // Lookahead to ensure we don't accept other constructs (for now disallow func(arg))

    // Manual lookahead using tokens vector via parser internals is not available here,
    // so we will scan using parser->next_token() repeatedly by temporarily consuming
    // and restoring position is not exposed. Instead, implement a safe conservative
    // check: inspect tokens until CBRACKET but reject if OPAREN found.

    // We'll implement the lookahead by peeking using next_token() repeatedly while
    // incrementing a local index via consuming and pushing back; but Parser doesn't
    // expose an API to restore index. Simpler: perform a single-pass parse that
    // rejects if an unexpected token (including OPAREN) appears.

    // Begin actual parsing (we already know current is OBRACKET)
    parser->consume_token(); // consume '['

    auto node = std::make_unique<ModuleAttrNode>();

    while (parser->not_eof() && parser->current_token().type != TokenType::CBRACKET) {
        if (parser->current_token().type == TokenType::IDENTIFIER) {
            node->attrs.push_back(parser->current_token().lexeme);
            parser->consume_token();
        } else if (parser->current_token().type == TokenType::COMMA) {
            parser->consume_token();
        } else {
            // Found something unexpected (e.g. OPAREN) — not a module attribute.
            // Rewind is not possible here, so signal failure by returning nullptr.
            // The caller should not have consumed the '[' in that case; but we already
            // consumed it. To be conservative, emit an error message and abort parsing
            // to avoid inconsistent parser state.
            parser->error("Invalid module attribute syntax (only simple identifiers allowed)");
            return nullptr;
        }
    }

    parser->expect(TokenType::CBRACKET, "Expected ']' to close module attribute");

    return node;
}
