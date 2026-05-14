#pragma once

#include "lsp/document.hpp"

#include <string>

namespace narval::lsp {

class NarvalAnalyzer {
public:
    DocumentState analyze(const std::string& uri, const std::string& text);
};

} // namespace narval::lsp
