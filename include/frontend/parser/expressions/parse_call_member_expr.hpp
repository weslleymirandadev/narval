#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

std::unique_ptr<Node> parse_call_member_expr(Parser* parser, std::unique_ptr<Node> statement);