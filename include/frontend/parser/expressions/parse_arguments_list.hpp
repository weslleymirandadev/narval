#pragma once
#include <memory>
#include <vector>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

std::vector<std::unique_ptr<Expr>> parse_arguments_list(Parser* parser);