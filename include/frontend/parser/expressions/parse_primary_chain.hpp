#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

// Parses a primary expression plus any postfix chain (calls, member access,
// array subscripts). Does NOT handle any binary operators — those are handled
// by the dedicated operator parsers higher in the hierarchy.
std::unique_ptr<Node> parse_primary_chain(Parser* parser, std::unique_ptr<Node> initial = nullptr);
