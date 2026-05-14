#pragma once

#include "lsp/json_utils.hpp"

#include "frontend/checker/checker.hpp"

namespace narval::lsp {

json diagnostic_to_json(const nv::Diagnostic& diagnostic);

} // namespace narval::lsp
