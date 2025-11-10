#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

std::unique_ptr<Node> parse_list_comp_expr(Parser* parser, std::unique_ptr<Expr> pre_parsed_elt = nullptr);