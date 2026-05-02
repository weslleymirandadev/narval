#include "frontend/parser/expressions/parse_or_expr.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"
#include "frontend/parser/expressions/parse_additive_expr.hpp"

// Parse the right-hand side of `or`:
//   { stmt* }   →  block handler
//   <expr>      →  value handler (supports calls, member access, arithmetic)
static std::unique_ptr<OrExprNode> parse_or_node(Parser* parser, std::unique_ptr<Node> base) {
    parser->consume_token(); // consume `or`

    if (parser->current_token().type == TokenType::OBRACE) {
        // Block handler
        parser->consume_token(); // consume `{`
        std::vector<std::unique_ptr<Node>> stmts;
        while (parser->current_token().type != TokenType::CBRACE &&
               parser->current_token().type != TokenType::EOF_TOKEN) {
            auto s = parse_stmt(parser);
            if (s) stmts.push_back(std::move(s));
        }
        parser->expect(TokenType::CBRACE, "Expected '}' after 'or' block");
        return std::make_unique<OrExprNode>(std::move(base), std::move(stmts));
    } else {
        // Value handler — supports calls, member access, binary arithmetic
        auto handler = parse_additive_expr(parser);
        return std::make_unique<OrExprNode>(std::move(base), std::move(handler));
    }
}

std::unique_ptr<Node> try_parse_or(Parser* parser, std::unique_ptr<Node> base) {
    while (parser->current_token().type == TokenType::OR_KW) {
        base = parse_or_node(parser, std::move(base));
    }
    return base;
}
