#include "frontend/parser/parser.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/ast/statements/throw_stmt_node.hpp"
#include <memory>

// Parse throw statement: throw exception_expression
std::unique_ptr<Node> parse_throw_stmt(Parser* parser) {
    // Consumir 'throw'
    parser->consume_token();
    
    // Parsear expressão da exceção
    auto exception_expr = parse_expr(parser);
    if (!exception_expr) {
        parser->error("Expected exception expression after throw");
        return nullptr;
    }
    
    // Opcional: consumir ';' se presente
    if (parser->current_token().type == TokenType::SEMICOLON) {
        parser->consume_token();
    }
    
    return std::make_unique<ThrowStatementNode>(std::move(exception_expr));
}
