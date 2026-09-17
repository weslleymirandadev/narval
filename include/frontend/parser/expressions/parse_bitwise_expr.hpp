#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

std::unique_ptr<Node> parse_bitwise_or_expr(Parser* parser);
std::unique_ptr<Node> parse_bitwise_xor_expr(Parser* parser);
std::unique_ptr<Node> parse_bitwise_and_expr(Parser* parser);
