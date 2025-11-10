#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

std::unique_ptr<Node> parse_postfix_expr(Parser* parser, std::unique_ptr<Node> expr);