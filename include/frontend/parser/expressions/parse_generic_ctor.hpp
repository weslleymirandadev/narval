#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

bool is_generic_ctor_start(Parser* p);
std::unique_ptr<Node> parse_generic_ctor_call(Parser* parser);
