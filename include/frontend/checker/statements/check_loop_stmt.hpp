#pragma once
#include <memory>
#include "frontend/checker/type.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/ast/ast.hpp"

std::shared_ptr<nv::Type>& check_loop_stmt(nv::Checker* ch, Node* node);
