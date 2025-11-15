#pragma once
#include <memory>
#include "frontend/ast/program.hpp"
#include "frontend/parser/parser.hpp"

std::unique_ptr<Node> parse_range_expr(Parser* parser);
