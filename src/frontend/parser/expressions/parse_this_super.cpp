#include "frontend/parser/expressions/parse_this_super.hpp"

std::unique_ptr<Node> parse_this_expr(Parser* parser) {
    parser->consume_token(); // consumir THIS
    return std::make_unique<ThisExprNode>();
}

std::unique_ptr<Node> parse_super_expr(Parser* parser) {
    parser->consume_token(); // consumir SUPER
    return std::make_unique<SuperExprNode>();
}
