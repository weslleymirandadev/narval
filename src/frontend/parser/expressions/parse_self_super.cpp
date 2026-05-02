#include "frontend/parser/expressions/parse_self_super.hpp"

std::unique_ptr<Node> parse_self_expr(Parser* parser) {
    parser->consume_token(); // consumir SELF
    return std::make_unique<SelfExprNode>();
}

std::unique_ptr<Node> parse_super_expr(Parser* parser) {
    parser->consume_token(); // consumir SUPER
    return std::make_unique<SuperExprNode>();
}
