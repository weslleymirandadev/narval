#pragma once
#include "frontend/ast/ast.hpp"
#include <memory>

// Parse try statement: try { try_block } catch(...) { catch_block } finally { finally_block }
std::unique_ptr<Node> parse_try_stmt(Parser* parser);
