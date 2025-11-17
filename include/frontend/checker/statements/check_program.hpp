#pragma once
#include <memory>
#include "frontend/checker/type.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/ast/ast.hpp"

std::shared_ptr<rph::Type>& check_program_stmt(rph::Checker* ch, Node* node);