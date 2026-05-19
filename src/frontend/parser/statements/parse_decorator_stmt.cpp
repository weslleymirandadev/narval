#include "frontend/parser/statements/parse_decorator_stmt.hpp"
#include "frontend/ast/statements/decorator_stmt_node.hpp"

std::unique_ptr<Node> parse_decorator_stmt(Parser* parser) {
    Token at_tok = parser->consume_token();

    auto node = std::make_unique<DecoratorStmtNode>();

    if (parser->current_token().type == TokenType::OBRACKET) {
        parser->error("Decorators accept only identifiers after '@'; use '@[name]' for attributes");
        return nullptr;
    }

    if (parser->current_token().type != TokenType::IDENTIFIER) {
        parser->error("Expected identifier after '@' for decorator");
        return nullptr;
    }

    Token name_tok = parser->consume_token();
    node->decorators.push_back(name_tok.lexeme);
    node->position = std::make_unique<PositionData>(
        at_tok.line,
        at_tok.column_start,
        name_tok.column_end,
        at_tok.position_start,
        name_tok.position_end,
        at_tok.filename
    );

    return node;
}
