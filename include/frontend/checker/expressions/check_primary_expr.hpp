#pragma once
#include <memory>
#include "frontend/checker/type.hpp"
#include "frontend/checker/checker.hpp"
#include "frontend/ast/ast.hpp"

std::unique_ptr<rph::Type> check_primary_expr(rph::Checker* ch, std::unique_ptr<Node> node);