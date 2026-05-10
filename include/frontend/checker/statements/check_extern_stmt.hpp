#pragma once
#include "frontend/checker/checker.hpp"
#include "frontend/ast/types.hpp"
#include <memory>

std::shared_ptr<nv::Type>& check_extern_stmt(nv::Checker* checker, Node* node);
