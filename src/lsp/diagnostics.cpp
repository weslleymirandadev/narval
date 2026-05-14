#include "lsp/diagnostics.hpp"

namespace narval::lsp {

json diagnostic_to_json(const nv::Diagnostic& diagnostic) {
    size_t line = diagnostic.line > 0 ? diagnostic.line - 1 : 0;
    size_t start = diagnostic.col_start > 0 ? diagnostic.col_start - 1 : 0;
    size_t end = diagnostic.col_end > 0 ? diagnostic.col_end - 1 : start;

    return {
        {"range", {
            {"start", {{"line", line}, {"character", start}}},
            {"end", {{"line", line}, {"character", end}}},
        }},
        {"severity", diagnostic.severity},
        {"source", "narval"},
        {"message", diagnostic.message},
    };
}

} // namespace narval::lsp
