#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"
std::unique_ptr<Node> parse_boolean_literal(Parser* p);