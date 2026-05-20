#pragma once
#include "frontend/parser/parser.hpp"
#include <memory>

std::unique_ptr<Node> parse_inline_asm_stmt(Parser* parser);
