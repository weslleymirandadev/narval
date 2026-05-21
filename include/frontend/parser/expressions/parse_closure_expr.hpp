#pragma once
#include <memory>
#include "frontend/ast/ast.hpp"
#include "frontend/parser/parser.hpp"

std::unique_ptr<Node> parse_closure_expr(Parser* parser);

// Retorna true se o token atual é '<' que inicia uma generic closure: <T>|...|
bool is_generic_closure_start(Parser* parser);
