#pragma once
#include "frontend/parser/parser.hpp"
#include "frontend/ast/types.hpp"
#include <memory>

std::unique_ptr<Node> parse_defer_stmt(Parser* parser);
