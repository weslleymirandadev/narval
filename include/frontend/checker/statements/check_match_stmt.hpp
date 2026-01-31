#pragma once
#include "frontend/checker/checker.hpp"
#include "frontend/ast/ast.hpp"

std::shared_ptr<nv::Type>& check_match_stmt(nv::Checker* checker, Node* node);
