#pragma once
#include "frontend/checker/checker.hpp"
#include "frontend/ast/expressions/closure_expr_node.hpp"

namespace nv {
std::shared_ptr<Type> check_closure_expr(ClosureExprNode* node, Checker& ch);
}
