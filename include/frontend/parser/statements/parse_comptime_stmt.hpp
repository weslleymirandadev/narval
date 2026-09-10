#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

// Parses every `comptime ...` construct:
//   comptime def f(...) { }   comptime X = <expr>   comptime { ... }
//   comptime for i in a..b { }   comptime if c { } else { }   comptime <expr>
std::unique_ptr<Node> parse_comptime_stmt(Parser* parser);

// `inline for ...` / `inline while ...` — Zig-style unrolled loops.
std::unique_ptr<Node> parse_inline_stmt(Parser* parser);
