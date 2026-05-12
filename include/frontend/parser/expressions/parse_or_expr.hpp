#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

std::unique_ptr<Node> try_parse_or(Parser* parser, std::unique_ptr<Node> base);
static std::unique_ptr<OrExprNode> parse_or_node(Parser* parser, std::unique_ptr<Expr> base);