#pragma once
#include "frontend/ast/ast.hpp"
#include <memory>

// Parse throw statement: throw exception_expression
std::unique_ptr<Node> parse_throw_stmt(Parser* parser);
