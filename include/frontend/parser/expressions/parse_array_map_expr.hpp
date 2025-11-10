#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

std::unique_ptr<Node> parse_array_map_expr(Parser* parser);