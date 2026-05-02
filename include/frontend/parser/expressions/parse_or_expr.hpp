#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

// Tries to wrap `base` in an OrExprNode if current token is OR_KW.
// Returns the (possibly wrapped) node.
std::unique_ptr<Node> try_parse_or(Parser* parser, std::unique_ptr<Node> base);
