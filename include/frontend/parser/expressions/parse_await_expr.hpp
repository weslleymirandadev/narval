#pragma once
#include "frontend/parser/parser.hpp"
#include "frontend/ast/types.hpp"
#include <memory>

std::unique_ptr<Node> parse_await_expr(Parser* parser);
