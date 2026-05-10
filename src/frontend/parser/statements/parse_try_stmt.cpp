#include "frontend/parser/parser.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/parser/statements/parse_stmt.hpp"
#include "frontend/parser/statements/parse_block_util.hpp"
#include "frontend/ast/statements/try_stmt_node.hpp"
#include <memory>

static std::vector<std::unique_ptr<Node>> parse_block_body(Parser* parser, const char* context) {
    if (parser->current_token().type != TokenType::OBRACE) {
        parser->error(std::string("Expected '{' ") + context);
        return {};
    }
    parser->consume_token();

    auto stmts_typed = parse_body(parser);
    std::vector<std::unique_ptr<Node>> stmts;
    for (auto& s : stmts_typed)
        if (s) stmts.push_back(std::unique_ptr<Node>(s.release()));

    if (parser->current_token().type != TokenType::CBRACE) {
        parser->error(std::string("Expected '}' ") + context);
        return stmts;
    }
    parser->consume_token();
    return stmts;
}

std::unique_ptr<CatchClause> parse_catch_clause(Parser* parser) {
    parser->consume_token(); // consume 'catch'

    std::string exception_type = "Error";
    if (parser->current_token().type == TokenType::IDENTIFIER) {
        exception_type = parser->current_token().lexeme;
        parser->consume_token();
    }

    std::string exception_var;
    if (parser->current_token().type == TokenType::IDENTIFIER) {
        exception_var = parser->current_token().lexeme;
        parser->consume_token();
    }

    auto body = parse_block_body(parser, "after catch clause");
    return std::make_unique<CatchClause>(exception_type, exception_var, std::move(body));
}

std::unique_ptr<Node> parse_try_stmt(Parser* parser) {
    parser->consume_token(); // consume 'try'

    auto try_body = parse_block_body(parser, "after try");

    std::vector<std::unique_ptr<CatchClause>> catches;
    while (parser->current_token().type == TokenType::CATCH) {
        auto clause = parse_catch_clause(parser);
        if (clause) catches.push_back(std::move(clause));
    }

    std::vector<std::unique_ptr<Node>> finally_body;
    if (parser->current_token().type == TokenType::FINALLY) {
        parser->consume_token();
        finally_body = parse_block_body(parser, "after finally");
    }

    return std::make_unique<TryStatementNode>(
        std::move(try_body),
        std::move(catches),
        std::move(finally_body)
    );
}
