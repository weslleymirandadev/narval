#pragma once
#include <memory>
#include <vector>
#include "frontend/ast/ast.hpp"
#include "frontend/ast/expressions/arg_node.hpp"
#include "frontend/parser/parser.hpp"

std::vector<std::unique_ptr<ArgNode>> parse_args(Parser* parser);