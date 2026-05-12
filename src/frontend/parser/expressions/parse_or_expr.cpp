#include "frontend/parser/expressions/parse_or_expr.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/expressions/parse_call_member_expr.hpp"
#include "frontend/parser/expressions/parse_additive_expr.hpp"
#include "frontend/ast/types.hpp"

static std::unique_ptr<OrExprNode> parse_or_node(Parser* parser, std::unique_ptr<Expr> base) {
    parser->consume_token();

    if (parser->current_token().type == TokenType::OBRACE) {
        parser->consume_token();
        std::vector<std::unique_ptr<Stmt>> stmts;
        while (parser->current_token().type != TokenType::CBRACE &&
               parser->current_token().type != TokenType::EOF_TOKEN) {
            auto s = parse_stmt(parser);
            if (s) stmts.push_back(std::unique_ptr<Stmt>(static_cast<Stmt*>(s.release())));
        }
        parser->expect(TokenType::CBRACE, "Expected '}' after 'or' block");
        return std::make_unique<OrExprNode>(std::move(base), std::move(stmts));
    } else {
        auto handler = parse_additive_expr(parser);
        return std::make_unique<OrExprNode>(std::move(base), std::unique_ptr<Expr>(static_cast<Expr*>(handler.release())));
    }
}

std::unique_ptr<Node> try_parse_or(Parser* parser, std::unique_ptr<Node> base) {
    while (parser->current_token().type == TokenType::OR_KW) {
        base = parse_or_node(parser, std::move(std::unique_ptr<Expr>(static_cast<Expr*>(base.release()))));
    }
    return base;
}
