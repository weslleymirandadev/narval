#include "frontend/parser/expressions/parse_await_expr.hpp"
#include "frontend/parser/expressions/parse_expr.hpp"
#include "frontend/ast/expressions/await_expr_node.hpp"

// Parse: `await expr`
std::unique_ptr<Node> parse_await_expr(Parser* parser) {
    parser->consume_token(); // consume 'await'
    auto operand = parse_expr(parser);
    if (!operand) {
        parser->error("Expected expression after 'await'");
        return nullptr;
    }
    return std::make_unique<AwaitExprNode>(std::move(operand));
}
