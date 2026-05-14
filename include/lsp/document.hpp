#pragma once

#include "frontend/checker/checker.hpp"
#include "frontend/lexer/token.hpp"

#include <string>
#include <vector>

namespace narval::lsp {

struct DocumentState {
    std::string uri;
    std::string path;
    std::string text;
    std::vector<Token> lexer_tokens;
    std::vector<nv::Diagnostic> diagnostics;
};

} // namespace narval::lsp
