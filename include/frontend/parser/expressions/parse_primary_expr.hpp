#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

std::unique_ptr<Node> parse_primary_expr(Parser* parser);