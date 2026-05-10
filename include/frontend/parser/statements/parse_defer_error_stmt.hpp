#pragma once
#include "frontend/parser/parser.hpp"
#include <memory>

std::unique_ptr<Node> parse_defer_error_stmt(Parser* parser);
