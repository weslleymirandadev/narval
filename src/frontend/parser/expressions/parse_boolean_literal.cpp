#include "frontend/parser/expressions/parse_boolean_literal.hpp"
#include <string>
std::unique_ptr<Node> parse_boolean_literal(Parser* parser) {
    Token t = parser->consume_token();
    size_t line = t.line;
    size_t column[2] =  {t.column_start, t.column_end};
    size_t position[2] = {  t.position_start, t.position_end };
    std::unique_ptr<PositionData> pos = std::make_unique<PositionData>(line, column[0], column[1], position[0], position[1]);
    std::unique_ptr<Node> boolean = std::make_unique<BooleanLiteralNode>(t.lexeme == "true");
    return boolean;
}